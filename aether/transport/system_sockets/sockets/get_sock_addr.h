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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_GET_SOCK_ADDR_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_GET_SOCK_ADDR_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__) || defined(ESP_PLATFORM) || defined(_WIN32)

#  define GET_SOCK_ADDR_ENABLED 1

#  if defined _WIN32
#    include <winsock2.h>
#    include <ws2def.h>
#    include <ws2ipdef.h>
#    include <mswsock.h>
#  endif
#  if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
      defined(__FreeBSD__)
#    include <netinet/in.h>
#  endif

#  if defined(ESP_PLATFORM)
#    include "lwip/sockets.h"
#  endif

#  include <cstddef>

#  include "aether/types/address.h"

namespace ae {
struct SockAddr {
  sockaddr* addr() { return reinterpret_cast<sockaddr*>(&data); }

  union {
#  if AE_SUPPORT_IPV4 == 1
    struct sockaddr_in ipv4;
#  endif
#  if AE_SUPPORT_IPV6 == 1
    struct sockaddr_in6 ipv6;
#  endif
  } data;

  std::size_t size;
};

SockAddr GetSockAddr(AddressPort const& ip_address_port);
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_GET_SOCK_ADDR_H_
