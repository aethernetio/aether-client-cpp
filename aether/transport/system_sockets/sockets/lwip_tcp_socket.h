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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_TCP_SOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_TCP_SOCKET_H_

#include "aether/config.h"
#include "aether/transport/system_sockets/sockets/lwip_socket.h"

#if AE_SUPPORT_TCP && LWIP_SOCKET_ENABLED
#  include "aether/poller/poller.h"

#  define LWIP_TCP_SOCKET_ENABLED 1

namespace ae {
class LwipTcpSocket final : public LwipSocket {
 public:
  explicit LwipTcpSocket(Ptr<IPoller> const& poller);

  ISocket& Connect(AddressPort const& destination,
                   ConnectedCb connected_cb) override;

 protected:
  void OnPollerEvent(DescriptorType fd, EventType event) override;

 private:
  static int MakeSocket() noexcept;

  void OnConnectionEvent(DescriptorType fd);

  ConnectionState connection_state_;
  ConnectedCb connected_cb_;
};
}  // namespace ae
#endif

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_TCP_SOCKET_H_
