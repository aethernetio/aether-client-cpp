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

#include "aether/transport/low_level/tcp/tcp.h"

#if defined COMMON_TCP_TRANSPORT_ENABLED

#  include <vector>
#  include <cstring>
#  include <utility>

#  include "aether/mstream.h"
#  include "aether/mstream_buffers.h"
#  include "aether/transport/transport_tele.h"

namespace ae {
constexpr std::size_t kMtuSelected = 1500;

TcpTransport::ConnectionAction::ConnectionAction(ActionContext action_context,
                                                 TcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      state_{State::kConnecting},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  Connect();
}

ActionResult TcpTransport::ConnectionAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kGetConnectionUpdate:
        ConnectionUpdate();
        break;
      case State::kConnected:
        return ActionResult::Result();
      case State::kConnectionFailed:
        return ActionResult::Error();
      default:
        break;
    }
  }
  return {};
}

void TcpTransport::ConnectionAction::Connect() {
  AE_TELE_INFO(kPosixTcpTransportConnect, "Connect to {}",
               transport_->endpoint_);

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
      break;
    }
  }

  AE_TELED_DEBUG("Waited for connection to {}", transport_->endpoint_);
  transport_->OnConnected();
  state_ = State::kConnected;
}

TcpTransport::SendAction::SendAction(ActionContext action_context,
                                     TcpTransport& transport, DataBuffer data,
                                     TimePoint current_time)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)},
      current_time_{current_time},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

TcpTransport::SendAction::SendAction(SendAction&& other) noexcept
    : SocketPacketSendAction{std::move(other)},
      transport_{other.transport_},
      data_{std::move(other.data_)},
      current_time_{other.current_time_},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

void TcpTransport::SendAction::Send() {
  state_ = State::kProgress;

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_lock_, std::adopt_lock};

  auto size_to_send = data_.size() - sent_offset_;
  auto res =
      transport_->socket_.Send(Span{data_.data() + sent_offset_, size_to_send});
  if (!res) {
    state_ = State::kFailed;
    return;
  }
  sent_offset_ += *res;

  if (sent_offset_ >= data_.size()) {
    state_ = State::kSuccess;
  }
}

TcpTransport::ReadAction::ReadAction(ActionContext action_context,
                                     TcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      read_buffer_(kMtuSelected) {}

ActionResult TcpTransport::ReadAction::Update() {
  if (read_event_.exchange(false)) {
    DataReceived();
  }
  if (error_.exchange(false)) {
    return ActionResult::Error();
  }

  return {};
}

void TcpTransport::ReadAction::Read() {
  auto lock = std::lock_guard{transport_->socket_lock_};
  while (true) {
    auto res = transport_->socket_.Receive(
        Span{read_buffer_.data(), read_buffer_.size()});

    if (!res) {
      error_ = true;
      break;
    }
    // No data yet
    if (*res == 0) {
      break;
    }
    data_packet_collector_.AddData(read_buffer_.data(), *res);
    read_event_ = true;
  }
  if (error_ || read_event_) {
    Action::Trigger();
  }
}

void TcpTransport::ReadAction::DataReceived() {
  auto lock = std::lock_guard{transport_->socket_lock_};
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(kPosixTcpTransportReceive, "Receive data size {}",
                  data.size());
    transport_->data_receive_event_.Emit(data, Now());
  }
}

TcpTransport::TcpTransport(ActionContext action_context,
                           IPoller::ptr const& poller,
                           IpAddressPort const& endpoint)
    : action_context_{action_context},
      poller_{poller},
      endpoint_{endpoint},
      connection_info_{},
      socket_{},
      socket_packet_queue_manager_{action_context_} {
  AE_TELE_INFO(kPosixTcpTransport, "Created unix tcp transport to endpoint {}",
               endpoint_);
  connection_info_.connection_state = ConnectionState::kUndefined;
}

TcpTransport::~TcpTransport() { Disconnect(); }

void TcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);
  connection_error_sub_ = connection_action_->ErrorEvent().Subscribe(
      [this](auto const& /* action */) { OnConnectionFailed(); });
}

ConnectionInfo const& TcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
TcpTransport::ConnectionSuccess() {
  return connection_success_event_;
}

ITransport::ConnectionErrorEvent::Subscriber TcpTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber TcpTransport::ReceiveEvent() {
  return data_receive_event_;
}

ActionView<PacketSendAction> TcpTransport::Send(DataBuffer data,
                                                TimePoint current_time) {
  AE_TELE_DEBUG(kPosixTcpTransportSend, "Send data size {} at {:%H:%M:%S}",
                data.size(), current_time);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

  auto send_action = socket_packet_queue_manager_.AddPacket(
      SendAction{action_context_, *this, std::move(packet_data), current_time});
  send_action_subs_.Push(
      send_action->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("Send error, disconnect!");
        ConnectionError();
        Disconnect();
      }));
  return send_action;
}

void TcpTransport::OnConnected() {
  connection_info_.connection_state = ConnectionState::kConnected;
  // 2 - for max packet size
  connection_info_.max_packet_size = kMtuSelected - 2;

  read_action_.emplace(action_context_, *this);
  read_action_error_sub_ =
      read_action_->ErrorEvent().Subscribe([this](auto const& /*action */) {
        AE_TELED_ERROR("Read error, disconnect!");
        OnConnectionFailed();
        Disconnect();
      });

  socket_error_action_ = SocketEventAction{action_context_};
  socket_error_subscription_ = socket_error_action_.ResultEvent().Subscribe(
      [this](auto const& /* action */) {
        OnConnectionFailed();
        Disconnect();
      });

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

  connection_success_event_.Emit();
}

void TcpTransport::OnConnectionFailed() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

void TcpTransport::ReadSocket() { read_action_->Read(); }

void TcpTransport::WriteSocket() {
  if (!socket_packet_queue_manager_.empty()) {
    socket_packet_queue_manager_.Send();
  }
}

void TcpTransport::ErrorSocket() {
  AE_TELED_ERROR("Socket error");
  auto lock = std::lock_guard{socket_lock_};
  socket_error_action_.Notify();
  AE_TELED_ERROR("Socket error");
}

void TcpTransport::Disconnect() {
  AE_TELE_INFO(kPosixTcpTransportDisconnect, "Disconnect from {}", endpoint_);
  connection_info_.connection_state = ConnectionState::kDisconnected;
  socket_error_subscription_.Reset();

  auto lock = std::lock_guard{socket_lock_};
  if (auto poller_ptr = poller_.Lock(); poller_ptr) {
    poller_ptr->Remove(static_cast<DescriptorType>(socket_));
  }
  socket_.Disconnect();
}
}  // namespace ae
#endif
