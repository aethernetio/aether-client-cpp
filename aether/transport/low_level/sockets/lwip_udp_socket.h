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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LWIP_UDP_SOCKET_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LWIP_UDP_SOCKET_H_

#include "aether/transport/low_level/sockets/lwip_socket.h"

#if LWIP_SOCKET_ENABLED

namespace ae {
class LwipUdpSocket final : public LwipSocket {
 public:
  LwipUdpSocket();

  std::size_t GetMaxPacketSize() const override;

 private:
  static int MakeSocket();
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_LWIP_UDP_SOCKET_H_
