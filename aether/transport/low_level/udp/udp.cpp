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

#include "aether/transport/low_level/udp/udp.h"

#if COMMON_UDP_TRANSPORT_ENABLED

#  include "aether/transport/transport_tele.h"

namespace ae {
UdpTransport::ReadAction::ReadAction(ActionContext action_context,
                                     UdpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      read_buffer_(transport_->socket_.GetMaxPacketSize()) {}

UpdateStatus UdpTransport::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    ReadEvent();
  }
  if (error_event_) {
    return UpdateStatus::Error();
  }
  if (stop_event_) {
    return UpdateStatus::Stop();
  }
  return {};
}

void UdpTransport::ReadAction::Read() {
  auto lock = std::lock_guard{transport_->socket_mutex_};

  while (true) {
    auto res = transport_->socket_.Receive(
        Span{read_buffer_.data(), read_buffer_.size()});
    if (!res) {
      AE_TELED_ERROR("Socket receiver error");
      error_event_ = true;
      break;
    }
    if (*res == 0) {
      // No more data to receive
      break;
    }
    auto& new_packet = read_buffers_.emplace_back();
    new_packet.reserve(*res);
    std::copy(std::begin(read_buffer_),
              std::begin(read_buffer_) + static_cast<std::ptrdiff_t>(*res),
              std::back_inserter(new_packet));

    read_event_ = true;
  }
  if (read_event_ || error_event_) {
    Action::Trigger();
  }
}

void UdpTransport::ReadAction::Stop() {
  stop_event_ = true;
  Action::Trigger();
}

void UdpTransport::ReadAction::ReadEvent() {
  auto lock = std::lock_guard{transport_->socket_mutex_};
  for (auto& packet : read_buffers_) {
    AE_TELE_DEBUG(kUdpTransportReceive, "Receive data size:{}", packet.size());
    transport_->out_data_event_.Emit(packet);
  }
  read_buffers_.clear();
}

UdpTransport::SendAction::SendAction(ActionContext action_context,
                                     UdpTransport& transport,
                                     DataBuffer&& data_buffer)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_buffer_{std::move(data_buffer)} {
  if (data_buffer_.size() > transport_->socket_.GetMaxPacketSize()) {
    AE_TELED_ERROR("Send message to big");
    state_ = State::kFailed;
  }
}

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
  auto lock = std::lock_guard{transport_->socket_mutex_, std::adopt_lock};

  assert(data_buffer_.size() <= transport_->socket_.GetMaxPacketSize());
  auto res =
      transport_->socket_.Send(Span{data_buffer_.data(), data_buffer_.size()});
  if (!res) {
    state_ = State::kFailed;
    return;
  }

  if (*res == 0) {
    // Not sent yet
    return;
  }
  if (*res != data_buffer_.size()) {
    AE_TELED_ERROR("Send error, sent size isn't same as packet size");
    state_ = State::kFailed;
    return;
  }
  state_ = State::kDone;
}

UdpTransport::UdpTransport(ActionContext action_context,
                           IPoller::ptr const& poller, IpAddressPort endpoint)
    : action_context_{std::move(action_context)},
      poller_{poller},
      endpoint_{std::move(endpoint)},
      send_queue_manager_{action_context_},
      notify_error_action_{action_context_} {
  AE_TELE_INFO(kUdpTransport);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = false;

  Connect();
}

UdpTransport::~UdpTransport() { Disconnect(); }

void UdpTransport::Connect() {
  AE_TELE_INFO(kUdpTransportConnect, "Udp connect to endpoint {}", endpoint_);

  auto res = socket_.Connect(endpoint_);
  // Udp must be immediately connected
  if (res != ISocket::ConnectionState::kConnected) {
    AE_TELE_ERROR(kUdpTransportConnectFailed,
                  "Failed to connect udp transport to endpoint {}", endpoint_);
    stream_info_.link_state = LinkState::kLinkError;
    return;
  }

  read_action_ = OwnActionPtr<ReadAction>{action_context_, *this};
  read_error_sub_ = read_action_->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELED_ERROR("Read error, disconnect!");
    stream_info_.link_state = LinkState::kLinkError;
    Disconnect();
  }});

  socket_error_sub_ =
      notify_error_action_->StatusEvent().Subscribe(OnResult{[this]() {
        AE_TELED_ERROR("Socket error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  auto poller_ptr = poller_.Lock();
  assert(poller_ptr);
  socket_event_sub_ =
      poller_ptr->Add(static_cast<DescriptorType>(socket_))
          .Subscribe([this](PollerEvent event) {
            if (event.descriptor != static_cast<DescriptorType>(socket_)) {
              return;
            }
            switch (event.event_type) {
              case EventType::kRead:
                ReadSocket();
                break;
              case EventType::kWrite:
                WriteSocket();
                break;
              case EventType::kError:
                ErrorSocket();
                break;
            }
          });

  stream_info_.link_state = LinkState::kLinked;
  stream_info_.rec_element_size = socket_.GetMaxPacketSize();
  stream_info_.max_element_size = socket_.GetMaxPacketSize();
  stream_info_.is_writable = true;
  stream_update_event_.Emit();
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
  AE_TELE_DEBUG(kUdpTransportSend, "Send data size:{}", in_data.size());
  auto send_packet_action = send_queue_manager_->AddPacket(
      ActionPtr<SendAction>{action_context_, *this, std::move(in_data)});

  send_action_error_subs_.Push(
      send_packet_action->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Send error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }}));

  return send_packet_action;
}

void UdpTransport::ReadSocket() { read_action_->Read(); }

void UdpTransport::WriteSocket() { send_queue_manager_->Send(); }

void UdpTransport::ErrorSocket() { notify_error_action_->Notify(); }

void UdpTransport::Disconnect() {
  AE_TELE_INFO(kUdpTransportDisconnect, "Disconnect from {}", endpoint_);
  stream_info_.is_writable = false;
  socket_error_sub_.Reset();

  auto lock = std::lock_guard{socket_mutex_};

  if (!socket_.IsValid()) {
    return;
  }

  if (auto poller_ptr = poller_.Lock(); poller_ptr) {
    poller_ptr->Remove(static_cast<DescriptorType>(socket_));
  }

  socket_.Disconnect();
  stream_update_event_.Emit();
}

}  // namespace ae

#endif
