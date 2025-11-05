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

#include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#if AE_SUPPORT_UDP && defined WIN_SOCKET_ENABLED

#  include <winsock2.h>
#  include <ws2def.h>
#  include <ws2ipdef.h>

#  include "aether/misc/defer.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace win_udp_socket_internal {
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
}  // namespace win_udp_socket_internal

WinUdpSocket::WinUdpSocket() : WinSocket{1200} {
  bool created = false;

  // ::socket() sets WSA_FLAG_OVERLAPPED by default that allows us to use iocp
  auto sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET) {
    AE_TELED_ERROR("Got socket creation error {}", WSAGetLastError());
    return;
  }
  // close socket on error
  defer[&] {
    if (!created) {
      ::closesocket(sock);
    }
  };

  // make socket non-blocking
  u_long nonblocking = 1;
  if (auto res = ::ioctlsocket(sock, FIONBIO, &nonblocking);
      res == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket make non-blocking error {}", WSAGetLastError());
    return;
  }

  created = true;
  socket_ = sock;
}

WinUdpSocket::~WinUdpSocket() { Disconnect(); }

WinUdpSocket::ConnectionState WinUdpSocket::Connect(
    IpAddressPort const& destination) {
  auto addr = win_udp_socket_internal::GetSockAddr(destination);
  auto res = connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (res == SOCKET_ERROR) {
    AE_TELED_ERROR("Socket connect error {}", WSAGetLastError());
    return ConnectionState::kConnectionFailed;
  }

  if (!RequestRecv()) {
    AE_TELED_ERROR("Request receive failed!");
    return ConnectionState::kConnectionFailed;
  }

  return ConnectionState::kConnected;
}

WinUdpSocket::ConnectionState WinUdpSocket::GetConnectionState() {
  int error = 0;
  int opt_len = sizeof(error);
  if (auto res = getsockopt(socket_, SOL_SOCKET, SO_ERROR,
                            reinterpret_cast<char*>(&error), &opt_len);
      res == SOCKET_ERROR) {
    AE_TELED_ERROR("Getsockopt returns error {}", res);
    return ConnectionState::kConnectionFailed;
  }

  if (error != 0) {
    AE_TELED_ERROR("Socket connect error {}", error);
    return ConnectionState::kConnectionFailed;
  }

  return ConnectionState::kConnected;
}

void WinUdpSocket::Disconnect() {
  if (socket_ == INVALID_SOCKET) {
    return;
  }
  closesocket(socket_);
  socket_ = INVALID_SOCKET;
}

}  // namespace ae
#endif
