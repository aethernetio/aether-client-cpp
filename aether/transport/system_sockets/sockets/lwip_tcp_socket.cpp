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

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

#  include "aether/misc/defer.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipTcpSocket::LwipTcpSocket() : LwipSocket{MakeSocket()} {}

std::size_t LwipTcpSocket::GetMaxPacketSize() const { return 1500; }

int LwipTcpSocket::MakeSocket() {
  bool created = false;
  constexpr int on = 1;

  auto sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) {
    AE_TELED_ERROR("Socket not created");
    return kInvalidSocket;
  }

  // close the socket if not created
  defer[&] {
    if (!created) {
      close(sock);
    }
  };

  // make socket nonblocking
  if (lwip_fcntl(sock, F_SETFL, O_NONBLOCK) != ESP_OK) {
    AE_TELED_ERROR("lwip_fcntl set nonblocking mode error {} {}", errno,
                   strerror(errno));
    return kInvalidSocket;
  }

  // set receive timeout on socket.
  timeval tv;
  tv.tv_sec = kRcvTimeoutSec;
  tv.tv_usec = kRcvTimeoutUsec;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() SO_RCVTIMEO on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    return kInvalidSocket;
  }

  // set reuse address on socket.
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() SO_REUSEADDR on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    return kInvalidSocket;
  }

  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() TCP_NODELAY on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    return kInvalidSocket;
  }

  AE_TELED_DEBUG("Socket created");
  created = true;
  return sock;
}

}  // namespace ae
#endif
