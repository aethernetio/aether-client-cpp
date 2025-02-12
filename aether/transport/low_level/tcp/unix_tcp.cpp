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

#include "aether/transport/low_level/tcp/unix_tcp.h"

#if defined UNIX_TCP_TRANSPORT_ENABLED

#  include <arpa/inet.h>
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>

#  include <cstring>
#  include <cerrno>
#  include <vector>
#  include <utility>

#  include "aether/mstream_buffers.h"
#  include "aether/mstream.h"
#  include "aether/tele/tele.h"

// Workaround for BSD and MacOS
#  if not defined SOL_TCP and defined IPPROTO_TCP
#    define SOL_TCP IPPROTO_TCP
#  endif

namespace ae {

UnixTcpTransport::ConnectionAction::ConnectionAction(
    ActionContext action_context, UnixTcpTransport& transport)
    : Action{action_context},
      endpoint_{transport.endpoint_},
      state_{State::kConnecting} {
  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  Connect();
}

TimePoint UnixTcpTransport::ConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kConnected:
        Action::Result(*this);
        break;
      case State::kNotConnected:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

int UnixTcpTransport::ConnectionAction::get_socket() const { return socket_; }
UnixTcpTransport::ConnectionAction::State
UnixTcpTransport::ConnectionAction::get_state() const {
  return state_.get();
}

void UnixTcpTransport::ConnectionAction::Connect() {
  AE_TELE_DEBUG("TcpTransportConnect", "Connect to {}", endpoint_);
  socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_ == kInvalidSocket) {
    AE_TELED_ERROR("Socket create error {} {}", errno, strerror(errno));
    state_.Set(State::kNotConnected);
    return;
  }
  int one = 1;
  auto ssopt_res = setsockopt(socket_, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
  if (ssopt_res == -1) {
    AE_TELED_ERROR("Socket set option error {} {}", errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
#  ifndef __unix__
  addr.sin_len = sizeof(addr);
#  endif  // __unix__
  addr.sin_family = AF_INET;
  assert(endpoint_.ip.version == IpAddress::Version::kIpV4);
  std::memcpy(&addr.sin_addr.s_addr, endpoint_.ip.value.ipv4_value, 4);
  addr.sin_port = ae::SwapToInet(endpoint_.port);

  auto r = connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (r == -1) {
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }
  AE_TELED_DEBUG("Connected to {}", endpoint_);
  state_.Set(State::kConnected);
}

UnixTcpTransport::UnixPacketSendAction::UnixPacketSendAction(
    ActionContext action_context, int socket, DataBuffer data,
    TimePoint current_time)
    : SocketPacketSendAction{action_context},
      socket_{socket},
      data_{std::move(data)},
      current_time_{current_time} {
  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

void UnixTcpTransport::UnixPacketSendAction::Send() {
  if (state_.get() == State::kQueued) {
    state_.Set(State::kProgress);
  }

  auto size_to_send = data_.size() - sent_offset_;
  auto r = send(socket_, data_.data() + sent_offset_, size_to_send, 0);

  if ((r >= 0) && (r < size_to_send)) {
    // save remaining data to later write
    sent_offset_ += static_cast<std::size_t>(r);
    return;
  }
  if (r == -1) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error {} {}", errno, strerror(errno));
      state_.Set(State::kFailed);
    }
    return;
  }
  state_.Set(State::kSuccess);
}

UnixTcpTransport::UnixTcpTransport(ActionContext action_context,
                                   IPoller::ptr poller,
                                   IpAddressPort const& endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      connection_info_{} {
  AE_TELE_DEBUG("TcpTransport", "Created unix tcp transport to endpoint {}",
                endpoint_);
  connection_info_.connection_state = ConnectionState::kUndefined;
}

UnixTcpTransport::~UnixTcpTransport() { Disconnect(); }

void UnixTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);
  connection_action_subscriptions_.Push(
      connection_action_->SubscribeOnResult(
          [this](auto const& action) { OnConnected(action.get_socket()); }),
      connection_action_->SubscribeOnError(
          [this](auto const& /* action */) { OnConnectionFailed(); }),
      connection_action_->FinishedEvent().Subscribe(
          [this]() { connection_action_.reset(); }));
}

ConnectionInfo const& UnixTcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
UnixTcpTransport::ConnectionSuccess() {
  return connection_success_event_;
}

ITransport::ConnectionErrorEvent::Subscriber
UnixTcpTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber UnixTcpTransport::ReceiveEvent() {
  return data_receive_event_;
}

ActionView<PacketSendAction> UnixTcpTransport::Send(DataBuffer data,
                                                    TimePoint current_time) {
  AE_TELE_DEBUG("TcpTransportSend", "Send data size {} at {:%H:%M:%S}",
                data.size(), current_time);
  assert(socket_ != kInvalidSocket);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

  return socket_packet_queue_manager_.AddPacket(UnixPacketSendAction{
      action_context_, socket_, std::move(packet_data), current_time});
}

void UnixTcpTransport::OnConnected(int socket) {
  connection_info_.connection_state = ConnectionState::kConnected;
  // TODO: get max packet size from socket
  connection_info_.max_packet_size = 1500 - 2;  // 2 - for max packet size
  socket_ = socket;

  socket_event_action_ = SocketEventAction{action_context_};
  socket_event_subscription_ =
      socket_event_action_.SubscribeOnResult([this](auto const& /* action */) {
        // TODO: Get time from action
        OnSocketEvent(TimePoint::clock::now());
      });

  poller_->Add(
      PollerEvent{socket_, EventType::ANY},
      [this](auto const& /* event */) { socket_event_action_.Notify(); });

  connection_success_event_.Emit();
}

void UnixTcpTransport::OnConnectionFailed() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

void UnixTcpTransport::OnSocketEvent(TimePoint current_time) {
  if (socket_ == kInvalidSocket) {
    return;
  }
  ReadSocket(current_time);
  WriteSocket();
}

void UnixTcpTransport::ReadSocket(TimePoint current_time) {
  int count;
  while (ioctl(socket_, FIONREAD, &count) == 0 && count > 0) {
    DataBuffer data(static_cast<size_t>(count));
    auto r = recv(socket_, data.data(), static_cast<std::size_t>(count), 0);
    if (r > 0) {
      AE_TELE_DEBUG("TcpTransportOnData", "Get data size {}\ndata: {}",
                    data.size(), data);
      data_packet_collector_.AddData(std::move(data));
    } else if (r < 1) {
      AE_TELED_ERROR("Recv error {} {}", errno, strerror(errno));
      Disconnect();
    } else if (r == 0) {
      AE_TELED_ERROR("Recv 0 while expected {}", count);
      Disconnect();
    }
  }
  OnDataReceived(current_time);
}

void UnixTcpTransport::WriteSocket() {
  if (!socket_packet_queue_manager_.empty()) {
    socket_packet_queue_manager_.Send();
  }
}

void UnixTcpTransport::OnDataReceived(TimePoint current_time) {
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG("TcpTransportReceive", "Receive data size {}", data.size());
    data_receive_event_.Emit(data, current_time);
  }
}

void UnixTcpTransport::Disconnect() {
  AE_TELE_DEBUG("TcpTransportDisconnect", "Disconnect from {}", endpoint_);
  connection_info_.connection_state = ConnectionState::kConnected;
  if (socket_ == kInvalidSocket) {
    return;
  }

  socket_event_subscription_.Reset();
  poller_->Remove(PollerEvent{socket_, {}});

  if (close(socket_) != 0) {
    return;
  }
  socket_ = kInvalidSocket;
}
}  // namespace ae
#endif
