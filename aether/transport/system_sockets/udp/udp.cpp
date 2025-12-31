/*
 * Copyright 2025 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "aether/transport/system_sockets/udp/udp.h"

#if SYSTEM_SOCKET_UDP_TRANSPORT_ENABLED

#  include "aether/transport/system_sockets/sockets/udp_sockets_factory.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
UdpTransport::ReadAction::ReadAction(ActionContext action_context,
                                     UdpTransport& transport)
    : Action{action_context}, transport_{&transport} {}

UpdateStatus UdpTransport::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    ReadEvent();
  }
  if (stop_event_) {
    return UpdateStatus::Stop();
  }
  return {};
}

void UdpTransport::ReadAction::Read(Span<std::uint8_t> data) {
  auto lock = std::scoped_lock{transport_->socket_mutex_};

  AE_TELED_DEBUG("Received data {}", data.size());
  auto& new_packet = read_buffers_.emplace_back();
  new_packet.reserve(data.size());
  std::copy(std::begin(data), std::end(data), std::back_inserter(new_packet));

  read_event_ = true;
  Action::Trigger();
}

void UdpTransport::ReadAction::Stop() {
  stop_event_ = true;
  Action::Trigger();
}

void UdpTransport::ReadAction::ReadEvent() {
  auto lock = std::scoped_lock{transport_->socket_mutex_};
  for (auto& packet : read_buffers_) {
    AE_TELE_DEBUG(kUdpTransportReceive, "Socket {} receive data size:{}",
                  transport_->endpoint_, packet.size());
    transport_->out_data_event_.Emit(packet);
  }
  read_buffers_.clear();
}

UdpTransport::SendAction::SendAction(ActionContext action_context,
                                     UdpTransport& transport,
                                     DataBuffer&& data_buffer)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_buffer_{std::move(data_buffer)} {}

UdpTransport::SendAction::SendAction(SendAction&& other) noexcept
    : SocketPacketSendAction{std::move(other)},
      transport_{other.transport_},
      data_buffer_{std::move(other.data_buffer_)} {}

void UdpTransport::SendAction::Send() {
  state_ = State::kInProgress;

  if (!transport_->socket_mutex_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::scoped_lock{std::adopt_lock, transport_->socket_mutex_};

  auto res =
      transport_->socket_->Send(Span{data_buffer_.data(), data_buffer_.size()});
  if (!res) {
    AE_TELED_ERROR("Data has not been written to {} size {}",
                   transport_->endpoint_, data_buffer_.size());
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }
  AE_TELED_DEBUG("Data has been written to {} size {}", transport_->endpoint_,
                 data_buffer_.size());

  if (*res == 0) {
    // Not sent yet
    return;
  }
  if (*res != data_buffer_.size()) {
    AE_TELED_ERROR("Send error, sent size isn't same as packet size");
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }
  state_ = State::kDone;
}

UdpTransport::UdpTransport(ActionContext action_context,
                           IPoller::ptr const& poller, AddressPort endpoint)
    : action_context_{std::move(action_context)},
      endpoint_{std::move(endpoint)},
      socket_{UdpSocketFactory::Create(*poller)},
      send_queue_manager_{action_context_},
      read_action_{action_context_, *this},
      notify_error_action_{action_context_} {
  AE_TELE_INFO(kUdpTransport);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = false;

  // Make connection
  AE_TELE_INFO(kUdpTransportConnect, "Udp connect to endpoint {}", endpoint_);

  socket_error_sub_ =
      notify_error_action_->StatusEvent().Subscribe(OnResult{[this]() {
        AE_TELED_ERROR("Socket error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  socket_->RecvData(MethodPtr<&UdpTransport::OnRecvData>{this})
      .ReadyToWrite(MethodPtr<&UdpTransport::OnReadyToWrite>{this})
      .Error(MethodPtr<&UdpTransport::OnSocketError>{this})
      .Connect(endpoint_, MethodPtr<&UdpTransport::OnConnected>{this});
}

UdpTransport::~UdpTransport() { Disconnect(); }

void UdpTransport::OnConnected(ISocket::ConnectionState connection_state) {
  switch (connection_state) {
    case ISocket::ConnectionState::kConnecting:
    case ISocket::ConnectionState::kNone:
    case ISocket::ConnectionState::kConnectionFailed:
    case ISocket::ConnectionState::kDisconnected: {
      // Udp must be immediately connected
      AE_TELE_ERROR(kUdpTransportConnectFailed,
                    "Failed to connect udp transport to endpoint {}",
                    endpoint_);
      stream_info_.link_state = LinkState::kLinkError;
      stream_update_event_.Emit();
      break;
    }
    case ISocket::ConnectionState::kConnected: {
      stream_info_.link_state = LinkState::kLinked;
      stream_info_.is_writable = true;
      stream_update_event_.Emit();
      break;
    }
  }
}

UdpTransport::StreamUpdateEvent::Subscriber
UdpTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo UdpTransport::stream_info() const { return stream_info_; }

UdpTransport::OutDataEvent::Subscriber UdpTransport::out_data_event() {
  return out_data_event_;
}

void UdpTransport::Restream() {
  AE_TELED_DEBUG("UDP restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

ActionPtr<StreamWriteAction> UdpTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kUdpTransportSend, "Socket {} send data size:{}", endpoint_,
                in_data.size());
  auto send_packet_action = send_queue_manager_->AddPacket(
      ActionPtr<SendAction>{action_context_, *this, std::move(in_data)});

  send_action_error_subs_ +=
      send_packet_action->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Send error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  return send_packet_action;
}

void UdpTransport::OnRecvData(Span<std::uint8_t> data) {
  read_action_->Read(data);
}

void UdpTransport::OnReadyToWrite() { send_queue_manager_->Send(); }

void UdpTransport::OnSocketError() { notify_error_action_->Notify(); }

void UdpTransport::Disconnect() {
  AE_TELE_INFO(kUdpTransportDisconnect, "Disconnect from {}", endpoint_);
  stream_info_.is_writable = false;
  socket_error_sub_.Reset();
  {
    auto lock = std::scoped_lock{socket_mutex_};
    if (!socket_) {
      return;
    }
    socket_->Disconnect();
    socket_.reset();
  }

  stream_update_event_.Emit();
}

}  // namespace ae

#endif
