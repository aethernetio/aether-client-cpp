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
#  include "aether/transport/system_sockets/sockets/win_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
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
  auto addr = win_socket_internal::GetSockAddr(destination);
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
