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

#  include "aether/mstream.h"
#  include "aether/mstream_buffers.h"
#  include "aether/transport/transport_tele.h"

namespace ae {
// Workaround for BSD and MacOS
#  if not defined SOL_TCP and defined IPPROTO_TCP
#    define SOL_TCP IPPROTO_TCP
#  endif

#  if not defined MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#  endif

namespace unix_tcp_internal {
inline bool SetTcpNoDelay(int sock) {
  constexpr int one = 1;
  auto res = setsockopt(sock, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
  if (res == -1) {
    AE_TELED_ERROR("Socket set option TCP_NODELAY error {} {}", errno,
                   strerror(errno));
    return false;
  }
  return true;
}

inline bool SetNoSigpipe([[maybe_unused]] int sock) {
#  if defined SO_NOSIGPIPE
  constexpr int one = 1;
  auto res = setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
  if (res == -1) {
    AE_TELED_ERROR("Socket set option SO_NOSIGPIPE error {} {}", errno,
                   strerror(errno));
    return false;
  }
#  endif
  return true;
}

inline bool SetNonblocking(int sock) {
  if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
    AE_TELED_ERROR("Socket set O_NONBLOCK error {} {}", errno, strerror(errno));
    return false;
  }
  return true;
}

struct SockAddr {
  sockaddr* addr() { return reinterpret_cast<sockaddr*>(&data); }

  union {
#  if AE_SUPPORT_IPV4 == 1
    struct sockaddr_in ipv4;
#  endif
#  if AE_SUPPORT_IPV6 == 1
    struct sockaddr_in6 ipv6;
#  endif
  } data;

  std::size_t size;
};

inline SockAddr GetSockAddr(IpAddressPort const& ip_address_port) {
  switch (ip_address_port.ip.version) {
    case IpAddress::Version::kIpV4:
#  if AE_SUPPORT_IPV4 == 1
    {
      SockAddr sock_addr{};
      sock_addr.size = sizeof(sock_addr.data.ipv4);
      auto& addr = sock_addr.data.ipv4;

#    ifndef __unix__
      addr.sin_len = sizeof(sockaddr_in);
#    endif  // __unix__
      std::memcpy(&addr.sin_addr.s_addr, ip_address_port.ip.value.ipv4_value,
                  4);
      addr.sin_port = ae::SwapToInet(ip_address_port.port);
      addr.sin_family = AF_INET;

      return sock_addr;
    }
#  else
    {
      assert(false);
      break;
    }
#  endif
    case IpAddress::Version::kIpV6: {
#  if AE_SUPPORT_IPV6 == 1
      SockAddr sock_addr;
      sock_addr.size = sizeof(sock_addr.data.ipv6);
      auto& addr = sock_addr.data.ipv6;
#    ifndef __unix__
      addr.sin6_len = sizeof(sockaddr_in6);
#    endif  // __unix__
      std::memcpy(&addr.sin6_addr, ip_address_port.ip.value.ipv6_value, 16);
      addr.sin6_port = ae::SwapToInet(ip_address_port.port);
      addr.sin6_family = AF_INET6;

      return sock_addr;
    }
#  else
      {
        assert(false);
        break;
      }
#  endif
  }
  return {};
}
}  // namespace unix_tcp_internal

constexpr std::size_t kMtuSelected = 1500;

UnixTcpTransport::ConnectionAction::ConnectionAction(
    ActionContext action_context, UnixTcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      state_{State::kConnecting},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  Connect();
}

TimePoint UnixTcpTransport::ConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kGetConnectionUpdate:
        ConnectionUpdate();
        break;
      case State::kConnected:
        Action::Result(*this);
        break;
      case State::kConnectionFailed:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

void UnixTcpTransport::ConnectionAction::Connect() {
  AE_TELE_DEBUG(TcpTransportConnect, "Connect to {}", transport_->endpoint_);
  socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_ == kInvalidSocket) {
    AE_TELED_ERROR("Socket create error {} {}", errno, strerror(errno));
    state_.Set(State::kConnectionFailed);
    return;
  }

  if (!unix_tcp_internal::SetNonblocking(socket_)) {
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kConnectionFailed);
    return;
  }

  if (!unix_tcp_internal::SetTcpNoDelay(socket_)) {
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kConnectionFailed);
    return;
  }

  if (!unix_tcp_internal::SetNoSigpipe(socket_)) {
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kConnectionFailed);
    return;
  }

  auto addr = unix_tcp_internal::GetSockAddr(transport_->endpoint_);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      WaitConnection();
    } else {
      AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
      close(socket_);
      socket_ = kInvalidSocket;
      state_ = State::kConnectionFailed;
    }
    return;
  }
  AE_TELED_DEBUG("Connected to {}", transport_->endpoint_);
  transport_->OnConnected(socket_);
  state_ = State::kConnected;
}

void UnixTcpTransport::ConnectionAction::WaitConnection() {
  state_ = State::kWaitConnection;

  auto poller_ptr = transport_->poller_;
  if (!poller_ptr) {
    AE_TELED_ERROR("Poller is null");
    state_ = State::kConnectionFailed;
    return;
  }

  poller_subscription_ = poller_ptr->Add(socket_).Subscribe([&](auto event) {
    if (event.descriptor != socket_) {
      return;
    }
    // remove poller first
    auto poller_ptr = transport_->poller_;
    assert(poller_ptr);
    poller_ptr->Remove(socket_);
    state_ = State::kGetConnectionUpdate;
  });
}

void UnixTcpTransport::ConnectionAction::ConnectionUpdate() {
  poller_subscription_.Reset();
  // check socket status
  int err;
  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error {}, {}", errno, strerror(errno));
    socket_ = kInvalidSocket;
    close(socket_);
    state_ = State::kConnectionFailed;
    return;
  }
  if (err != 0) {
    AE_TELED_ERROR("Connect error {}, {}", err, strerror(err));
    socket_ = kInvalidSocket;
    close(socket_);
    state_ = State::kConnectionFailed;
    return;
  }

  AE_TELED_DEBUG("Waited for connection to {}", transport_->endpoint_);
  transport_->OnConnected(socket_);
  state_ = State::kConnected;
}

UnixTcpTransport::UnixPacketSendAction::UnixPacketSendAction(
    ActionContext action_context, UnixTcpTransport& transport, DataBuffer data,
    TimePoint current_time)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{std::move(data)},
      current_time_{current_time},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

UnixTcpTransport::UnixPacketSendAction::UnixPacketSendAction(
    UnixPacketSendAction&& other) noexcept
    : SocketPacketSendAction{std::move(other)},
      transport_{other.transport_},
      data_{std::move(other.data_)},
      current_time_{other.current_time_},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

void UnixTcpTransport::UnixPacketSendAction::Send() {
  state_ = State::kProgress;

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_lock_, std::adopt_lock};

  auto size_to_send = data_.size() - sent_offset_;
  // add nosignal to prevent throw SIGPIPE and handle it manually
  int flags = MSG_NOSIGNAL;
  auto res = send(transport_->socket_, data_.data() + sent_offset_,
                  size_to_send, flags);
  if ((res >= 0) && (res < size_to_send)) {
    // save remaining data to later write
    sent_offset_ += static_cast<std::size_t>(res);
    return;
  }
  if (res == -1) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error {} {}", errno, strerror(errno));
      state_ = State::kFailed;
    }
    return;
  }
  state_ = State::kSuccess;
}

UnixTcpTransport::UnixPacketReadAction::UnixPacketReadAction(
    ActionContext action_context, UnixTcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      read_buffer_(kMtuSelected) {}

TimePoint UnixTcpTransport::UnixPacketReadAction::Update(
    TimePoint current_time) {
  if (read_event_.exchange(false)) {
    DataReceived(current_time);
  }
  if (error_.exchange(false)) {
    Action::Error(*this);
  }

  return current_time;
}

void UnixTcpTransport::UnixPacketReadAction::Read() {
  auto lock = std::lock_guard{transport_->socket_lock_};
  while (true) {
    auto res =
        recv(transport_->socket_, read_buffer_.data(), read_buffer_.size(), 0);
    if (res < 0) {
      // No data
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
        break;
      }
      AE_TELED_ERROR("Recv error {} {}", errno, strerror(errno));
      error_ = true;
      break;
    }
    if (res == 0) {
      // socket shutdown
      AE_TELED_ERROR("Recv shutdown");
      error_ = true;
      break;
    }
    data_packet_collector_.AddData(read_buffer_.data(),
                                   static_cast<std::size_t>(res));
    read_event_ = true;
  }
  if (error_ || read_event_) {
    Action::Trigger();
  }
}

void UnixTcpTransport::UnixPacketReadAction::DataReceived(
    TimePoint current_time) {
  auto lock = std::lock_guard{transport_->socket_lock_};
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(TcpTransportReceive, "Receive data size {}", data.size());
    transport_->data_receive_event_.Emit(data, current_time);
  }
}

UnixTcpTransport::UnixTcpTransport(ActionContext action_context,
                                   IPoller::ptr poller,
                                   IpAddressPort const& endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      connection_info_{},
      socket_packet_queue_manager_{action_context_} {
  AE_TELE_DEBUG(TcpTransport, "Created unix tcp transport to endpoint {}",
                endpoint_);
  connection_info_.connection_state = ConnectionState::kUndefined;
}

UnixTcpTransport::~UnixTcpTransport() { Disconnect(); }

void UnixTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);
  connection_error_sub_ = connection_action_->ErrorEvent().Subscribe(
      [this](auto const& /* action */) { OnConnectionFailed(); });
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
  AE_TELE_DEBUG(TcpTransportSend, "Send data size {} at {:%H:%M:%S}",
                data.size(), current_time);
  assert(socket_ != kInvalidSocket);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

  auto send_action =
      socket_packet_queue_manager_.AddPacket(UnixPacketSendAction{
          action_context_, *this, std::move(packet_data), current_time});
  send_action_subs_.Push(
      send_action->ErrorEvent().Subscribe([this](auto const&) {
        AE_TELED_ERROR("Send error, disconnect!");
        Disconnect();
      }));
  return send_action;
}

void UnixTcpTransport::OnConnected(int socket) {
  connection_info_.connection_state = ConnectionState::kConnected;
  // 2 - for max packet size
  connection_info_.max_packet_size = kMtuSelected - 2;
  socket_ = socket;

  read_action_.emplace(action_context_, *this);
  read_action_error_sub_ =
      read_action_->ErrorEvent().Subscribe([this](auto const& /*action */) {
        AE_TELED_ERROR("Read error, disconnect!");
        Disconnect();
      });

  socket_error_action_ = SocketEventAction{action_context_};
  socket_error_subscription_ = socket_error_action_.ResultEvent().Subscribe(
      [this](auto const& /* action */) { Disconnect(); });

  socket_poll_subscription_ =
      poller_->Add(socket_).Subscribe([this](auto const& event) {
        if (event.descriptor != socket_) {
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

void UnixTcpTransport::OnConnectionFailed() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

void UnixTcpTransport::ReadSocket() { read_action_->Read(); }

void UnixTcpTransport::WriteSocket() {
  if (!socket_packet_queue_manager_.empty()) {
    socket_packet_queue_manager_.Send();
  }
}

void UnixTcpTransport::ErrorSocket() {
  AE_TELED_ERROR("Socket error");
  auto lock = std::lock_guard{socket_lock_};
  socket_error_action_.Notify();

  int err;
  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error {}, {}", errno, strerror(errno));
    return;
  }
  AE_TELED_ERROR("Socket error {}, {}", err, strerror(err));
}

void UnixTcpTransport::Disconnect() {
  AE_TELE_DEBUG(TcpTransportDisconnect, "Disconnect from {}", endpoint_);
  connection_info_.connection_state = ConnectionState::kDisconnected;
  socket_error_subscription_.Reset();

  auto lock = std::lock_guard{socket_lock_};
  if (socket_ == kInvalidSocket) {
    return;
  }

  poller_->Remove(socket_);

  shutdown(socket_, SHUT_RDWR);

  if (close(socket_) != 0) {
    return;
  }
  socket_ = kInvalidSocket;

  OnConnectionFailed();
}
}  // namespace ae
#endif
