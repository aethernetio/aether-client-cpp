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

#include "aether/transport/system_sockets/sockets/udp_sockets_factory.h"

#if AE_SUPPORT_UDP

// IWYU pragma: begin_exports
#  include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#  include "aether/transport/system_sockets/sockets/unix_udp_socket.h"
#  include "aether/transport/system_sockets/sockets/lwip_cb_udp_socket.h"
#  include "aether/transport/system_sockets/sockets/lwip_udp_socket.h"
// IWYU pragma: end_exports

namespace ae {
std::unique_ptr<ISocket> UdpSocketFactory::Create(
    [[maybe_unused]] IPoller& poller) {
#  if UNIX_SOCKET_ENABLED
  return std::make_unique<UnixUdpSocket>(poller);
#  elif LWIP_CB_UDP_SOCKET_ENABLED
  return std::make_unique<LwipCBUdpSocket>();
#  elif LWIP_SOCKET_ENABLED
  return std::make_unique<LwipUdpSocket>(poller);
#  elif WIN_SOCKET_ENABLED
  return std::make_unique<WinUdpSocket>(poller);
#  else
  static_assert(false, "Unsupported platform");
  return nullptr;
#  endif
}

}  // namespace ae
#endif
