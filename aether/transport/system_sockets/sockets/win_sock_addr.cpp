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

#include "aether/transport/system_sockets/sockets/win_sock_addr.h"

#if WIN_SOCK_ADDR_ENABLED

#  include "aether/env.h"

namespace ae::win_socket_internal {
SockAddr GetSockAddr(AddressPort const& ip_address_port) {
  SockAddr sock_addr{};
  std::visit(reflect::OverrideFunc{
#  if AE_SUPPORT_IPV4 == 1
                 [&](IpV4Addr const& ipv4) {
                   sock_addr.size = sizeof(sock_addr.data.ipv4);
                   auto& addr = sock_addr.data.ipv4;
                   std::memcpy(&addr.sin_addr.s_addr, ipv4.ipv4_value, 4);
                   addr.sin_port = ae::SwapToInet(ip_address_port.port);
                   addr.sin_family = AF_INET;
                 },
#  endif
#  if AE_SUPPORT_IPV6 == 1
                 [&](IpV6Addr const& ipv6) {
                   sock_addr.size = sizeof(sock_addr.data.ipv6);
                   auto& addr = sock_addr.data.ipv6;
                   std::memcpy(&addr.sin6_addr, ipv6.ipv6_value, 16);
                   addr.sin6_port = ae::SwapToInet(ip_address_port.port);
                   addr.sin6_family = AF_INET6;
                 },
#  endif
                 [](auto&&) { assert(false && "Unsupported address type"); }},
             ip_address_port.address);

  return sock_addr;
}
}  // namespace ae::win_socket_internal
#endif
