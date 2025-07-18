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

#include "aether/transport/low_level/sockets/lwip_udp_socket.h"
#if LWIP_SOCKET_ENABLED

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

#  include "aether/misc/defer.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipUdpSocket::LwipUdpSocket() : LwipSocket{MakeSocket()} {}

std::size_t LwipUdpSocket::GetMaxPacketSize() const { return 1200; }

int LwipUdpSocket::MakeSocket() {
  bool created = false;
  auto sock = socket(AF_INET, SOCK_DGRAM, 0);
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

  AE_TELED_DEBUG("Socket created");
  created = true;
  return sock;
}
}  // namespace ae

#endif  // LWIP_SOCKET_ENABLED
