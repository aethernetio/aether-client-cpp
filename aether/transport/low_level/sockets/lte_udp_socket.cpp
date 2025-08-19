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

#include "aether/transport/low_level/sockets/lte_udp_socket.h"
#if LTE_SOCKET_ENABLED

#  include "aether/misc/defer.h"

#  include "aether/tele/tele.h"

namespace ae {
LteUdpSocket::LteUdpSocket() : LteSocket{MakeSocket()} {}

std::size_t LteUdpSocket::GetMaxPacketSize() const { return 1200; }

int LteUdpSocket::MakeSocket() {
  int sock = 0;

  AE_TELED_DEBUG("Socket created");
  return sock;
}
}  // namespace ae

#endif  // LWIP_SOCKET_ENABLED
