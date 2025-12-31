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

#include "aether/transport/system_sockets/sockets/lwip_get_addr.h"

#if (defined(ESP_PLATFORM))

#  include <variant>
#  include <cassert>

#  include "aether/reflect/reflect.h"

namespace ae {
std::optional<ip_addr_t> LwipGetAddr(const AddressPort& addr_port) {
  return std::visit(
      reflect::OverrideFunc{
#  if AE_SUPPORT_IPV4
          [&](IpV4Addr const& ipv4) -> std::optional<ip_addr_t> {
            ip_addr_t lwip_addr;
            IP_ADDR4(&lwip_addr, ipv4.ipv4_value[0], ipv4.ipv4_value[1],
                     ipv4.ipv4_value[2], ipv4.ipv4_value[3]);
            return lwip_addr;
          },
#  endif
#  if AE_SUPPORT_IPV6
          [&](IpV6Addr const& ipv6) -> std::optional<ip_addr_t> {
            ip_addr_t lwip_addr;
            std::array<std::uint32_t const*, 4> ip6_parts{};
            for (auto i = 0; i < 4; ++i) {
              ip6_parts[i] = reinterpret_cast<std::uint32_t const*>(
                  &ipv6.ipv6_value[i * 4]);
            }
            IP_ADDR6(&lwip_addr, PP_HTONL(*ip6_parts[0]),
                     PP_HTONL(*ip6_parts[1]), PP_HTONL(*ip6_parts[2]),
                     PP_HTONL(*ip6_parts[3]));
            return lwip_addr;
          },
#  endif
          [](auto const&) -> std::optional<ip_addr_t> {
            assert(false && "Unsupported address type");
            return std::nullopt;
          },
      },
      addr_port.address);
}
}  // namespace ae
#endif  // (defined(ESP_PLATFORM))
