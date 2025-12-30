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
  
namespace ae {
std::optional<ip_addr_t> convert_address_port_to_lwip(const AddressPort& addr_port) {
    ip_addr_t lwip_addr;
    
    if (addr_port.address.Index() == AddrVersion::kIpV4) {
        const auto& ipv4 = addr_port.address.Get<IpV4Addr>();
        IP_ADDR4(&lwip_addr, 
                ipv4.ipv4_value[0],
                ipv4.ipv4_value[1],
                ipv4.ipv4_value[2],
                ipv4.ipv4_value[3]);
        return lwip_addr;
    }
    else if (addr_port.address.Index() == AddrVersion::kIpV6) {
        const auto& ipv6 = addr_port.address.Get<IpV6Addr>();
        
        // LwIP expects a 4-element uint32_t array for IPv6
        uint32_t ip6_parts[4];
        std::memcpy(ip6_parts, ipv6.ipv6_value, 16);
        
        IP_ADDR6(&lwip_addr,
                PP_HTONL(ip6_parts[0]),
                PP_HTONL(ip6_parts[1]),
                PP_HTONL(ip6_parts[2]),
                PP_HTONL(ip6_parts[3]));
        return lwip_addr;
    }
    else if (addr_port.address.Index() == AddrVersion::kNamed) {        
        return std::nullopt; // Name could not be resolved
    }
    
    return std::nullopt; // Unsupported address type
}
}  // namespace ae
#endif  // (defined(ESP_PLATFORM))
