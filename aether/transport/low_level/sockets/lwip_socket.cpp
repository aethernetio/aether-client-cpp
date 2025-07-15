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

#include "aether/transport/low_level/sockets/lwip_socket.h"

#if LWIP_SOCKET_ENABLED

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

#  include "aether/tele/tele.h"

extern "C" int lwip_hook_ip6_input(struct pbuf* p, struct netif* inp)
    __attribute__((weak));
extern "C" int lwip_hook_ip6_input(struct pbuf* p, struct netif* inp) {
  if (ip6_addr_isany_val(inp->ip6_addr[0].u_addr.ip6)) {
    // We don't have an LL address -> eat this packet here, so it won't get
    // accepted on input netif
    pbuf_free(p);
    return 1;
  }
  return 0;
}

namespace ae {
namespace lwip_socket_internal {
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

inline SockAddr GetSockAddr(IpAddressPort const& ip_address_port) {
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
      assert(false);
      break;
    }
#  endif
    case IpAddress::Version::kIpV6: {
#  if AE_SUPPORT_IPV6 == 1
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
        assert(false);
        break;
      }
#  endif
  }
  return {};
}
}  // namespace lwip_socket_internal

LwipSocket::LwipSocket(int socket) : socket_{socket} {}

LwipSocket::~LwipSocket() { Disconnect(); }

LwipSocket::operator DescriptorType() const { return DescriptorType{socket_}; }

LwipSocket::ConnectionState LwipSocket::Connect(
    IpAddressPort const& destination) {
  assert(socket_ != kInvalidSocket);

  auto sock_addr = lwip_socket_internal::GetSockAddr(destination);
  auto res = connect(socket_, sock_addr.addr(), sock_addr.size);
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      return ConnectionState::kConnecting;
    }
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    return ConnectionState::kConnectionFailed;
  }
  return ConnectionState::kConnected;
}

LwipSocket::ConnectionState LwipSocket::GetConnectionState() {
  // check socket status
  int err;
  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void*>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error {}, {}", errno, strerror(errno));
    return ConnectionState::kConnectionFailed;
  }

  if (err != 0) {
    AE_TELED_ERROR("Connect error {}, {}", err, strerror(err));
    return ConnectionState::kConnectionFailed;
  }

  return ConnectionState::kConnected;
}

void LwipSocket::Disconnect() {
  if (socket_ == kInvalidSocket) {
    return;
  }
  shutdown(socket_, SHUT_RDWR);
  if (close(socket_) != 0) {
    return;
  }
  socket_ = kInvalidSocket;
}

std::optional<std::size_t> LwipSocket::Send(Span<std::uint8_t> data) {
  auto size_to_send = data.size();
  auto res = send(socket_, data.data(), size_to_send, 0);
  if (res == -1) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error {} {}", errno, strerror(errno));
      return std::nullopt;
    }
    return 0;
  }
  return static_cast<std::size_t>(res);
}

std::optional<std::size_t> LwipSocket::Receive(Span<std::uint8_t> data) {
  auto res = recv(socket_, data.data(), data.size(), 0);
  if (res < 0) {
    // No data
    if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
      return 0;
    }
    AE_TELED_ERROR("Recv error {} {}", errno, strerror(errno));
    return std::nullopt;
  }
  // probably the socket is shutdown
  if (res == 0) {
    AE_TELED_ERROR("Recv shutdown");
    return std::nullopt;
  }
  return static_cast<std::size_t>(res);
}

bool LwipSocket::IsValid() const { return socket_ != kInvalidSocket; }

}  // namespace ae
#endif
