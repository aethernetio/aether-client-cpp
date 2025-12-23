/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/transport/system_sockets/tcp/tcp.h"

#if defined COMMON_TCP_TRANSPORT_ENABLED

#  include <vector>
#  include <limits>
#  include <utility>

#  include "aether/mstream.h"
#  include "aether/mstream_buffers.h"
#  include "aether/transport/system_sockets/sockets/tcp_sockets_factory.h"

#  include "aether/transport/transport_tele.h"

namespace ae {
TcpTransport::SendAction::SendAction(ActionContext action_context,
                                     TcpTransport& transport, DataBuffer data)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)} {}

void TcpTransport::SendAction::Send() {
  state_ = State::kInProgress;

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::scoped_lock{std::adopt_lock, transport_->socket_lock_};

  auto size_to_send = data_.size() - sent_offset_;
  auto res = transport_->socket_->Send(
      Span{data_.data() + sent_offset_, size_to_send});
  if (!res) {
    AE_TELED_ERROR("Data has not been written to {} size {}",
                   transport_->endpoint_, size_to_send)
    state_ = State::kFailed;
    Action::Trigger();
    return;
  }
  AE_TELED_DEBUG("Data has been written to {} size {}", transport_->endpoint_,
                 *res);
  sent_offset_ += *res;

  if (sent_offset_ >= data_.size()) {
    state_ = State::kDone;
    Action::Trigger();
  }
}

TcpTransport::ReadAction::ReadAction(ActionContext action_context,
                                     TcpTransport& transport)
    : Action{action_context}, transport_{&transport} {}

UpdateStatus TcpTransport::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    DataReceived();
  }
  if (stop_event_) {
    return UpdateStatus::Stop();
  }

  return {};
}

void TcpTransport::ReadAction::Read(Span<std::uint8_t> buffer) {
  auto lock = std::scoped_lock{transport_->socket_lock_};
  AE_TELED_DEBUG("Received data {}", buffer.size());
  data_packet_collector_.AddData(buffer.data(), buffer.size());
  read_event_ = true;
  Action::Trigger();
}

void TcpTransport::ReadAction::Stop() {
  stop_event_ = true;
  Action::Trigger();
}

void TcpTransport::ReadAction::DataReceived() {
  auto lock = std::scoped_lock{transport_->socket_lock_};
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kTcpTransportReceive, "Socket {} received data size {}",
                  transport_->endpoint_, data.size());
    transport_->out_data_event_.Emit(data);
  }
}

TcpTransport::TcpTransport(ActionContext action_context,
                           IPoller::ptr const& poller, AddressPort endpoint)
    : action_context_{action_context},
      endpoint_{std::move(endpoint)},
      stream_info_{},
      socket_{TcpSocketsFactory::Create(*poller)},
      send_queue_manager_{action_context_},
      read_action_{action_context_, *this},
      socket_connect_action_{action_context_},
      socket_error_action_{action_context_} {
  AE_TELE_INFO(kTcpTransport);

  socket_error_sub_ =
      socket_error_action_->StatusEvent().Subscribe(OnResult{[this]() {
        AE_TELED_ERROR("Socket error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  socket_connect_sub_ =
      socket_connect_action_->StatusEvent().Subscribe(ActionHandler{
          OnResult{[this]() {
            stream_info_.is_writable = true;
            stream_info_.link_state = LinkState::kLinked;
            stream_update_event_.Emit();
          }},
          OnError{[this]() {
            stream_info_.link_state = LinkState::kLinkError;
            stream_update_event_.Emit();
          }},
      });

  AE_TELE_INFO(kTcpTransportConnect, "Tcp connect to endpoint {}", endpoint_);

  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  // TODO: find a better value for max element size
  stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();

  // Make connection
  socket_->RecvData(MethodPtr<&TcpTransport::OnRecvData>{this})
      .ReadyToWrite(MethodPtr<&TcpTransport::OnReadyToWrite>{this})
      .Error(MethodPtr<&TcpTransport::OnSocketError>{this})
      .Connect(endpoint_, MethodPtr<&TcpTransport::OnConnection>{this});
}

TcpTransport::~TcpTransport() {
  stream_info_.link_state = LinkState::kUnlinked;
  Disconnect();
}

ActionPtr<StreamWriteAction> TcpTransport::Write(DataBuffer&& in_data) {
  AE_TELE_DEBUG(kTcpTransportSend, "Socket {} send data size {}", endpoint_,
                in_data.size());

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << in_data;

  auto send_action = send_queue_manager_->AddPacket(
      ActionPtr<SendAction>{action_context_, *this, std::move(packet_data)});
  send_action_subs_ += send_action->StatusEvent().Subscribe(  //~(^.^)~
      OnError{[this]() {
        AE_TELED_ERROR("Send error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});
  return send_action;
}

TcpTransport::StreamUpdateEvent::Subscriber
TcpTransport::stream_update_event() {
  return stream_update_event_;
}

StreamInfo TcpTransport::stream_info() const { return stream_info_; }

TcpTransport::OutDataEvent::Subscriber TcpTransport::out_data_event() {
  return out_data_event_;
}

void TcpTransport::Restream() {
  AE_TELED_DEBUG("TCP restream");
  stream_info_.link_state = LinkState::kLinkError;
  Disconnect();
}

void TcpTransport::OnConnection(ISocket::ConnectionState connection_state) {
  switch (connection_state) {
    case ISocket::ConnectionState::kConnecting:
      break;
    case ISocket::ConnectionState::kNone:
    case ISocket::ConnectionState::kConnectionFailed:
    case ISocket::ConnectionState::kDisconnected: {
      socket_connect_action_->Failed();
      break;
    }
    case ISocket::ConnectionState::kConnected: {
      socket_connect_action_->Notify();
      break;
    }
  }
}

void TcpTransport::OnRecvData(Span<std::uint8_t> data) {
  read_action_->Read(data);
}

void TcpTransport::OnReadyToWrite() { send_queue_manager_->Send(); }

void TcpTransport::OnSocketError() {
  AE_TELED_ERROR("Socket error");
  socket_error_action_->Notify();
}

void TcpTransport::Disconnect() {
  AE_TELE_INFO(kTcpTransportDisconnect, "Disconnect from {}", endpoint_);
  stream_info_.is_writable = false;
  socket_error_sub_.Reset();

  {
    auto lock = std::scoped_lock{socket_lock_};
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
