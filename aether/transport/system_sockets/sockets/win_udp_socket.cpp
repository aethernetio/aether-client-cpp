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
#  include <ws2tcpip.h>
#  if AE_BENCH_TCP_PROXY
#    include <cstdlib>
#    include <cstring>
#  endif

#  include "aether-miscpp/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele.h"

namespace ae {

// IOCP recv buffer must hold one complete datagram; cloud servers may aggregate
// payloads larger than the preferred send size without changing StreamInfo MTU.
static constexpr std::size_t kWinUdpRecvDatagramBufferBytes = 65536;

WinUdpSocket::WinUdpSocket(Ptr<IPoller> const& poller)
    : WinSocket{*poller, kWinUdpRecvDatagramBufferBytes} {
  bool created = false;

  // ::socket() sets WSA_FLAG_OVERLAPPED by default that allows us to use iocp
  auto sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == INVALID_SOCKET) {
    AETHER_TELED_ERROR("Got socket creation error {}", WSAGetLastError());
    return;
  }
  // close socket on error
  ae_defer[&] {
    if (!created) {
      ::closesocket(sock);
    }
  };

  // make socket non-blocking
  u_long nonblocking = 1;
  if (auto res = ::ioctlsocket(sock, static_cast<long>(FIONBIO), &nonblocking);
      res == SOCKET_ERROR) {
    AETHER_TELED_ERROR("Socket make non-blocking error {}", WSAGetLastError());
    return;
  }

  created = true;
  socket_ = sock;
}

WinUdpSocket::~WinUdpSocket() = default;

ISocket& WinUdpSocket::Connect(AddressPort const& destination,
                               ConnectedCb connected_cb) {
  assert(socket_ != INVALID_SOCKET);

  ConnectionState connection_state{ConnectionState::kNone};

  ae_defer[&]() {
    Poll();
    connected_cb(connection_state);
  };

  {
    auto lock = std::scoped_lock{socket_lock_};

    auto addr = GetSockAddr(destination);
#  if AE_BENCH_TCP_PROXY
    // Private benchmark relay: one loopback control datagram binds this socket
    // to its real endpoint. Subsequent datagrams retain their original bytes.
    unsigned char relay_header[6]{};
    char relay_port[16]{};
    auto const relay_length = GetEnvironmentVariableA(
        "AE_BENCH_UDP_PROXY_PORT", relay_port, sizeof(relay_port));
    bool const relayed = relay_length > 0;
    if (relayed) {
      char* end{}; auto port = std::strtoul(relay_port, &end, 10);
      if (relay_length >= sizeof(relay_port) || !port || port > 65535 ||
          *end || addr.data.ipv4.sin_family != AF_INET) {
        connection_state = ConnectionState::kConnectionFailed; return *this;
      }
      std::memcpy(relay_header, &addr.data.ipv4.sin_addr.s_addr, 4);
      std::memcpy(relay_header + 4, &addr.data.ipv4.sin_port, 2);
      addr.data.ipv4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      addr.data.ipv4.sin_port = htons(static_cast<unsigned short>(port));
    }
#  endif
    auto res =
        connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res == SOCKET_ERROR) {
      AETHER_TELED_ERROR("Socket connect error {}", WSAGetLastError());
      connection_state = ConnectionState::kConnectionFailed;
      return *this;
    }
#  if AE_BENCH_TCP_PROXY
    if (relayed && send(socket_, reinterpret_cast<char const*>(relay_header),
                        static_cast<int>(sizeof(relay_header)), 0) !=
                       static_cast<int>(sizeof(relay_header))) {
      connection_state = ConnectionState::kConnectionFailed; return *this;
    }
#  endif
  }

  if (!RequestRecv()) {
    AETHER_TELED_ERROR("Request receive failed!");
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state = ConnectionState::kConnected;
  return *this;
}
}  // namespace ae
#endif
