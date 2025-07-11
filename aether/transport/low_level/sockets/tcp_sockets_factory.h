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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_TCP_SOCKETS_FACTORY_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_TCP_SOCKETS_FACTORY_H_

// IWYU pragma: begin_exports
#include "aether/transport/low_level/sockets/win_tcp_socket.h"
#include "aether/transport/low_level/sockets/unix_tcp_socket.h"
#include "aether/transport/low_level/sockets/lwip_tcp_socket.h"
// IWYU pragma: end_exports

namespace ae {
#if UNIX_SOCKET_ENABLED
using TcpSocket = UnixTcpSocket;
#elif LWIP_SOCKET_ENABLED
using TcpSocket = LwipTcpSocket;
#elif WIN_SOCKET_ENABLED
using TcpSocket = WinTcpSocket;
#endif
}  // namespace ae

#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_TCP_SOCKETS_FACTORY_H_
