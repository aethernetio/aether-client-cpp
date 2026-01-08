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

#include "aether/transport/system_sockets/sockets/unix_udp_socket.h"

#if AE_SUPPORT_UDP && UNIX_SOCKET_ENABLED
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/udp.h>

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
UnixUdpSocket::UnixUdpSocket(IPoller& poller)
    : UnixSocket{poller, MakeSocket()} {
  // 1200 is our default MTU for UDP
  recv_buffer_.resize(1200);
}

int UnixUdpSocket::MakeSocket() {
  bool created = false;
  auto sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == kInvalidSocket) {
    AE_TELED_ERROR("Socket creation error {} {}", errno, strerror(errno));
    return kInvalidSocket;
  }

  // close socket on error
  defer[&] {
    if (!created) {
      close(sock);
    }
  };

  // make socket non-blocking
  if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
    AE_TELED_ERROR("Socket set O_NONBLOCK error {} {}", errno, strerror(errno));
    return kInvalidSocket;
  }
  created = true;
  return sock;
}

ISocket& UnixUdpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  // UDP connection means binding socket to a destination address
  assert((socket_ != kInvalidSocket) && "Socket is not initialized");
  ConnectionState connection_state{ConnectionState::kNone};
  defer[&]() {
    Poll();
    connected_cb(connection_state);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto addr = GetSockAddr(destination);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state = ConnectionState::kConnected;
  return *this;
}

}  // namespace ae
#endif
