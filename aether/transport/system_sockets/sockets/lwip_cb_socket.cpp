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

#include "aether/transport/system_sockets/sockets/lwip_cb_socket.h"

#if LWIP_SOCKET_ENABLED

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace lwip_cb_socket_internal {
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

inline SockAddr GetSockAddr(AddressPort const& ip_address_port) {
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
}  // namespace lwip_cb_socket_internal

LwipCBSocket::LwipCBSocket(int socket) : socket_{socket} {}

LwipCBSocket::~LwipCBSocket() { Disconnect(); }

LwipCBSocket::operator DescriptorType() const { return DescriptorType{socket_}; }

LwipCBSocket::ConnectionState LwipSocket::Connect(
    AddressPort const& destination) {
  assert(socket_ != kInvalidSocket);

  auto sock_addr = lwip_socket_internal::GetSockAddr(destination);

  return ConnectionState::kConnected;
}

LwipCBSocket::ConnectionState LwipSocket::GetConnectionState() {
  // check socket status

  return ConnectionState::kConnected;
}

void LwipCBSocket::Disconnect() {
  if (socket_ == kInvalidSocket) {
    return;
  }

  socket_ = kInvalidSocket;
}

std::optional<std::size_t> LwipCBSocket::Send(Span<std::uint8_t> data) {

  return {};
}

std::optional<std::size_t> LwipCBSocket::Receive(Span<std::uint8_t> data) {

  return {};
}

bool LwipCBSocket::IsValid() const { return socket_ != kInvalidSocket; }

}  // namespace ae
#endif
