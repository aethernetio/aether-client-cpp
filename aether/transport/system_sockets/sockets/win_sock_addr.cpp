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
SockAddr GetSockAddr(IpAddressPort const& ip_address_port) {
  switch (ip_address_port.ip.version) {
    case IpAddress::Version::kIpV4:
#  if AE_SUPPORT_IPV4 == 1
    {
      SockAddr sock_addr{};
      sock_addr.size = sizeof(sock_addr.data.ipv4);
      auto& addr = sock_addr.data.ipv4;

      std::memcpy(&addr.sin_addr.s_addr, ip_address_port.ip.value.ipv4_value,
                  4);
      addr.sin_port = ae::SwapToInet(ip_address_port.port);
      addr.sin_family = AF_INET;

      return sock_addr;
    }
#  else
    {
      assert(false && "IPv4 is not supported");
      break;
    }
#  endif
    case IpAddress::Version::kIpV6:
#  if AE_SUPPORT_IPV6 == 1
    {
      SockAddr sock_addr;
      sock_addr.size = sizeof(sock_addr.data.ipv6);
      auto& addr = sock_addr.data.ipv6;
      std::memcpy(&addr.sin6_addr, ip_address_port.ip.value.ipv6_value, 16);
      addr.sin6_port = ae::SwapToInet(ip_address_port.port);
      addr.sin6_family = AF_INET6;

      return sock_addr;
    }
#  else
    {
      assert(false && "IPv6 is not supported");
      break;
    }
#  endif
  }
  return {};
}
}  // namespace ae::win_socket_internal
#endif
