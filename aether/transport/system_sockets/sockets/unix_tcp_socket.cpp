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

#include "aether/transport/system_sockets/sockets/unix_tcp_socket.h"

#if AE_SUPPORT_TCP && UNIX_SOCKET_ENABLED

#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>

#  include <cerrno>

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

// Workaround for BSD and MacOS
#  if not defined SOL_TCP and defined IPPROTO_TCP
#    define SOL_TCP IPPROTO_TCP
#  endif

namespace ae {
namespace unix_tcp_socket_internal {
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
}  // namespace unix_tcp_socket_internal

UnixTcpSocket::UnixTcpSocket(IPoller& poller)
    : UnixSocket(poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1500 is our default MTU for TCP
  recv_buffer_.resize(1500);
}

int UnixTcpSocket::MakeSocket() {
  bool created = false;
  // TCP socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == kInvalidSocket) {
    AE_TELED_DEBUG("Socket creation error {} {}", errno, strerror(errno));
    return kInvalidSocket;
  }

  // close the socket if it fails to setup
  defer[&] {
    if (!created) {
      close(sock);
    }
  };

  if (!unix_tcp_socket_internal::SetNonblocking(sock)) {
    return kInvalidSocket;
  }
  if (!unix_tcp_socket_internal::SetTcpNoDelay(sock)) {
    return kInvalidSocket;
  }
  if (!unix_tcp_socket_internal::SetNoSigpipe(sock)) {
    return kInvalidSocket;
  }
  created = true;
  return sock;
}

ISocket& UnixTcpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  assert((socket_ != kInvalidSocket) && "Socket is not initialized");
  connected_cb_ = std::move(connected_cb);

  defer[&]() {
    // add poll for all events to detect connection
    poller_->Event(socket_,
                   EventType::kRead | EventType::kError | EventType::kWrite,
                   MethodPtr<&UnixTcpSocket::OnPollerEvent>{this});
    connected_cb_(connection_state_);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto addr = GetSockAddr(destination);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      connection_state_ = ConnectionState::kConnecting;
      return *this;
    }
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void UnixTcpSocket::OnPollerEvent(EventType event) {
  if (connection_state_ == ConnectionState::kConnecting) {
    OnConnectionEvent();
    return;
  }
  UnixSocket::OnPollerEvent(event);
}

void UnixTcpSocket::OnConnectionEvent() {
  defer[&]() {
    if (connected_cb_) {
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
