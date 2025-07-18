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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_TCP_SOCKET_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_TCP_SOCKET_H_

#include "aether/transport/low_level/sockets/win_socket.h"

#if defined WIN_SOCKET_ENABLED

#  include "winsock2.h"

namespace ae {
class WinTcpSocket final : public WinSocket {
  using ConnectExPtr = bool (*)(DescriptorType::Socket, sockaddr const*, int,
                                void*, DWORD, LPDWORD, LPOVERLAPPED);

 public:
  WinTcpSocket();
  ~WinTcpSocket() override;

  ConnectionState Connect(IpAddressPort const& destination) override;
  ConnectionState GetConnectionState() override;
  void Disconnect() override;

 private:
  ConnectExPtr GetConnectEx();
  bool InitConnection();

  bool connection_initiated_{false};
  WinPollerOverlapped conn_overlapped_;
};
}  // namespace ae
#endif
#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_WIN_TCP_SOCKET_H_
