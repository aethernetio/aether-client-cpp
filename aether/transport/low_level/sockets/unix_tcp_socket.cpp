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

#include "aether/transport/low_level/sockets/unix_tcp_socket.h"

#if UNIX_SOCKET_ENABLED

#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>

#  include <cerrno>

#  include "aether/misc/defer.h"

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

UnixTcpSocket::UnixTcpSocket() : UnixSocket(MakeSocket()) {}

std::size_t UnixTcpSocket::GetMaxPacketSize() const { return 1500; }

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
}  // namespace ae
#endif
