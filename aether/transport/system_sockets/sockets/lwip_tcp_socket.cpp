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

#include "aether/transport/system_sockets/sockets/lwip_tcp_socket.h"

#if AE_SUPPORT_TCP && LWIP_SOCKET_ENABLED

#  include "lwip/sockets.h"

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace lwip_tcp_socket_internal {
inline bool SetNonblocking(int sock) {
  if (lwip_fcntl(sock, F_SETFL, O_NONBLOCK) != ESP_OK) {
    AE_TELED_ERROR("Socket set O_NONBLOCK error {} {}", errno, strerror(errno));
    return false;
  }
  return true;
}

inline bool SetTcpNoDelay(int sock, int on) {
  auto res = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  if (res != ESP_OK) {
    AE_TELED_ERROR("Socket set option TCP_NODELAY error {} {}", errno,
                   strerror(errno));
    return false;
  }
  return true;
}

inline bool SetReuseAddress(int sock, int on) {
  auto res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (res != ESP_OK) {
    AE_TELED_ERROR("Socket set option SO_REUSEADDR error {} {}", errno,
                   strerror(errno));
    return false;
  }
  return true;
}

inline bool SetReciveTimeouts(int sock, int tv_sec, int tv_usec) {
  // set receive timeout on socket.
  timeval tv;
  tv.tv_sec = tv_sec;
  tv.tv_usec = tv_usec;
  auto res = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  if (res != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() SO_RCVTIMEO on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    return false;
  }
  return true;
}
}  // namespace lwip_tcp_socket_internal

LwipTcpSocket::LwipTcpSocket(IPoller& poller)
    : LwipSocket(poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1500 is our default MTU for TCP
  recv_buffer_.resize(1500);
}

int LwipTcpSocket::MakeSocket() {
  bool created = false;
  constexpr int on = 1;

  auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    AE_TELED_ERROR("LwIp TCP socket creation error {} {}", errno,
                   strerror(errno));
    return kInvalidSocket;
  }

  // close the socket if not created
  defer[&] {
    if (!created) {
      close(sock);
    }
  };

  if (!lwip_tcp_socket_internal::SetNonblocking(sock)) {
    return kInvalidSocket;
  }
  if (!lwip_tcp_socket_internal::SetTcpNoDelay(sock, on)) {
    return kInvalidSocket;
  }
  if (!lwip_tcp_socket_internal::SetReuseAddress(sock, on)) {
    return kInvalidSocket;
  }

  if (!lwip_tcp_socket_internal::SetReciveTimeouts(sock, kRcvTimeoutSec,
                                                   kRcvTimeoutUsec)) {
    return kInvalidSocket;
  }

  AE_TELED_DEBUG("LwIp TCP socket created");
  created = true;
  return sock;
}

ISocket& LwipTcpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  assert((socket_ != kInvalidSocket) && "Socket is not initialized");
  connected_cb_ = std::move(connected_cb);

  defer[&]() {
    // wait for all events to detect connection
    poller_->Event(socket_,
                   EventType::kRead | EventType::kWrite | EventType::kError,
                   MethodPtr<&LwipTcpSocket::OnPollerEvent>{this});
    connected_cb_(connection_state_);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto addr = GetSockAddr(destination);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      connection_state_ = ConnectionState::kConnecting;
      Poll();
      connected_cb_(connection_state_);
      return *this;
    }
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void LwipTcpSocket::OnPollerEvent(EventType event) {
  if (connection_state_ == ConnectionState::kConnecting) {
    OnConnectionEvent();
    return;
  }
  LwipSocket::OnPollerEvent(event);
}

void LwipTcpSocket::OnConnectionEvent() {
  defer[&]() {
    if (connected_cb_) {
      AE_TELED_DEBUG("LwIp TCP socket connectioin event {}", connection_state_);
      connected_cb_(connection_state_);
    }
  };

  // check socket status
  auto sock_err = GetSocketError();
  if (!sock_err || (sock_err.value() != 0)) {
    AE_TELED_ERROR("Connect error {}, {}", sock_err,
                   strerror(sock_err.value_or(0)));
    connection_state_ = ConnectionState::kConnectionFailed;
    return;
  }

  AE_TELED_DEBUG("Socket connected");
  connection_state_ = ConnectionState::kConnected;
}
}  // namespace ae
#endif
