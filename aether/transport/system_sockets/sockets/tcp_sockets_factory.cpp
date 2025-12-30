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

#include "aether/transport/system_sockets/sockets/tcp_sockets_factory.h"

#if AE_SUPPORT_TCP

// IWYU pragma: begin_exports
#  include "aether/transport/system_sockets/sockets/win_tcp_socket.h"
#  include "aether/transport/system_sockets/sockets/unix_tcp_socket.h"
#  include "aether/transport/system_sockets/sockets/lwip_cb_tcp_socket.h"
#  include "aether/transport/system_sockets/sockets/lwip_tcp_socket.h"
// IWYU pragma: end_exports

namespace ae {
std::unique_ptr<ISocket> TcpSocketsFactory::Create(
    [[maybe_unused]] IPoller& poller) {
#  if UNIX_SOCKET_ENABLED
  return std::make_unique<UnixTcpSocket>(poller);
#  elif LWIP_CB_TCP_SOCKET_ENABLED
  return std::make_unique<LwipCBTcpSocket>();
#  elif LWIP_SOCKET_ENABLED
  return std::make_unique<LwipTcpSocket>(poller);
#  elif WIN_SOCKET_ENABLED
  return std::make_unique<WinTcpSocket>(poller);
#  else
  static_assert(false, "Unsupported platform");
  return nullptr;
#  endif
}

}  // namespace ae
#endif
