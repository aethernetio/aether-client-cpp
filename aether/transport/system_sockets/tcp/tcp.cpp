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
#  include "aether/transport/transport_tele.h"

namespace ae {
TcpTransport::ConnectionAction::ConnectionAction(ActionContext action_context,
                                                 TcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      state_{State::kConnecting},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  Connect();
}

UpdateStatus TcpTransport::ConnectionAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kGetConnectionUpdate:
        ConnectionUpdate();
        break;
      case State::kConnected:
        return UpdateStatus::Result();
      case State::kConnectionFailed:
        return UpdateStatus::Error();
      case State::kStopped:
        return UpdateStatus::Stop();
      default:
        break;
    }
  }
  return {};
}

void TcpTransport::ConnectionAction::Stop() { state_ = State::kStopped; }

void TcpTransport::ConnectionAction::Connect() {
  AE_TELE_INFO(kTcpTransportConnect, "Connect to {}", transport_->endpoint_);

  auto res = transport_->socket_.Connect(transport_->endpoint_);
  switch (res) {
    case ISocket::ConnectionState::kConnected: {
      AE_TELED_DEBUG("Connected to {}", transport_->endpoint_);
      state_ = State::kConnected;
      break;
    }
    case ISocket::ConnectionState::kConnecting: {
      AE_TELED_DEBUG("Wait connection to {}", transport_->endpoint_);
      WaitConnection();
      break;
    }
    default: {
      AE_TELED_ERROR("Failed to connect to {}", transport_->endpoint_);
      transport_->socket_.Disconnect();
      state_ = State::kConnectionFailed;
      break;
    }
  }
}

void TcpTransport::ConnectionAction::WaitConnection() {
  state_ = State::kWaitConnection;

  auto poller_ptr = transport_->poller_.Lock();
  if (!poller_ptr) {
    AE_TELED_ERROR("Poller is null");
    state_ = State::kConnectionFailed;
    return;
  }

  poller_subscription_ =
      poller_ptr->Add(static_cast<DescriptorType>(transport_->socket_))
          .Subscribe([&](auto event) {
            if (event.descriptor !=
                static_cast<DescriptorType>(transport_->socket_)) {
              return;
            }
            // remove poller first
            auto poller_ptr = transport_->poller_.Lock();
            assert(poller_ptr);
            poller_ptr->Remove(
                static_cast<DescriptorType>(transport_->socket_));
            state_ = State::kGetConnectionUpdate;
          });
}

void TcpTransport::ConnectionAction::ConnectionUpdate() {
  poller_subscription_.Reset();
  // check socket status

  auto res = transport_->socket_.GetConnectionState();
  switch (res) {
    case ISocket::ConnectionState::kConnected: {
      AE_TELED_DEBUG("Connected to {}", transport_->endpoint_);
      state_ = State::kConnected;
      break;
    }
    default: {
      AE_TELED_ERROR("Failed to connect to {}", transport_->endpoint_);
      transport_->socket_.Disconnect();
      state_ = State::kConnectionFailed;
      return;
    }
  }

  AE_TELED_DEBUG("Waited for connection to {}", transport_->endpoint_);
  transport_->OnConnected();
  state_ = State::kConnected;
}

TcpTransport::SendAction::SendAction(ActionContext action_context,
                                     TcpTransport& transport, DataBuffer data)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

void TcpTransport::SendAction::Send() {
  state_ = State::kInProgress;

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_lock_, std::adopt_lock};

  auto size_to_send = data_.size() - sent_offset_;
  auto res =
      transport_->socket_.Send(Span{data_.data() + sent_offset_, size_to_send});
  if (!res) {
    AE_TELED_ERROR("Data has not been written to {} size {}",
                   transport_->endpoint_, size_to_send)
    state_ = State::kFailed;
    return;
  }
  AE_TELED_DEBUG("Data has been written to {} size {}", transport_->endpoint_,
                 *res);
  sent_offset_ += *res;

  if (sent_offset_ >= data_.size()) {
    state_ = State::kDone;
  }
}

TcpTransport::ReadAction::ReadAction(ActionContext action_context,
                                     TcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      read_buffer_(transport_->socket_.GetMaxPacketSize()) {}

UpdateStatus TcpTransport::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    DataReceived();
  }
  if (error_event_) {
    return UpdateStatus::Error();
  }
  if (stop_event_) {
    return UpdateStatus::Stop();
  }

  return {};
}

void TcpTransport::ReadAction::Read() {
  auto lock = std::scoped_lock{transport_->socket_lock_};
  while (true) {
    auto res = transport_->socket_.Receive(
        Span{read_buffer_.data(), read_buffer_.size()});

    if (!res) {
      error_event_ = true;
      break;
    }
    // No data yet
    if (*res == 0) {
      break;
    }
    AE_TELED_DEBUG("Received data {}", *res);
    data_packet_collector_.AddData(read_buffer_.data(), *res);
    read_event_ = true;
  }
  if (error_event_ || read_event_) {
    Action::Trigger();
  }
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
                           IPoller::ptr const& poller,
                           AddressPort const& endpoint)
    : action_context_{action_context},
      poller_{poller},
      endpoint_{endpoint},
      stream_info_{},
      socket_{},
      send_queue_manager_{action_context_} {
  AE_TELE_INFO(kTcpTransport, "Created tcp transport to endpoint {}",
               endpoint_);
  stream_info_.link_state = LinkState::kUnlinked;
  stream_info_.is_reliable = true;
  // TODO: find a better value for max element size
  stream_info_.max_element_size = std::numeric_limits<std::uint32_t>::max();

  // Make connection
  connection_action_ = OwnActionPtr<ConnectionAction>{action_context_, *this};
  connection_sub_ = connection_action_->StatusEvent().Subscribe(OnError{[&]() {
    stream_info_.link_state = LinkState::kLinkError;
    Disconnect();
  }});
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
  send_action_subs_.Push(send_action->StatusEvent().Subscribe(OnError{[this]() {
    AE_TELED_ERROR("Send error, disconnect!");
    stream_info_.link_state = LinkState::kLinkError;
    Disconnect();
  }}));
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

void TcpTransport::OnConnected() {
  stream_info_.is_writable = true;
  stream_info_.link_state = LinkState::kLinked;
  // 2 - for max packet size
  stream_info_.rec_element_size = socket_.GetMaxPacketSize() - 2;
  read_action_ = OwnActionPtr<ReadAction>{action_context_, *this};
  read_action_error_sub_ =
      read_action_->StatusEvent().Subscribe(OnError{[this]() {
        AE_TELED_ERROR("Read error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  socket_error_action_ = OwnActionPtr<SocketEventAction>{action_context_};
  socket_error_subscription_ =
      socket_error_action_->StatusEvent().Subscribe(OnResult{[this]() {
        AE_TELED_ERROR("Socket error, disconnect!");
        stream_info_.link_state = LinkState::kLinkError;
        Disconnect();
      }});

  auto poller_ptr = poller_.Lock();
  assert(poller_ptr);

  socket_poll_subscription_ =
      poller_ptr->Add(static_cast<DescriptorType>(socket_))
          .Subscribe([this](auto const& event) {
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

  stream_update_event_.Emit();
}

void TcpTransport::ReadSocket() { read_action_->Read(); }

void TcpTransport::WriteSocket() { send_queue_manager_->Send(); }

void TcpTransport::ErrorSocket() {
  AE_TELED_ERROR("Socket error");
  auto lock = std::scoped_lock{socket_lock_};
  socket_error_action_->Notify();
}

void TcpTransport::Disconnect() {
  AE_TELE_INFO(kTcpTransportDisconnect, "Disconnect from {}", endpoint_);
  stream_info_.is_writable = false;
  socket_error_subscription_.Reset();

  {
    auto lock = std::scoped_lock{socket_lock_};
    if (!socket_.IsValid()) {
      return;
    }
    if (auto poller_ptr = poller_.Lock(); poller_ptr) {
      poller_ptr->Remove(static_cast<DescriptorType>(socket_));
    }
    socket_.Disconnect();
  }

  stream_update_event_.Emit();
}
}  // namespace ae
#endif
