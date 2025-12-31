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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_UDP_SOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_UDP_SOCKET_H_

#if (defined(ESP_PLATFORM))

#  define LWIP_CB_UDP_SOCKET_ENABLED 1

#  include "lwip/udp.h"
#  include "lwip/ip_addr.h"

#  include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
/**
 * \brief Base implementation of LWIP socket.
 */
class LwipCBUdpSocket : public ISocket {
 public:
  static constexpr int kInvalidSocket = -1;

  explicit LwipCBUdpSocket();
  ~LwipCBUdpSocket() override;

  ISocket& ReadyToWrite(ReadyToWriteCb ready_to_write_cb) override;
  ISocket& RecvData(RecvDataCb recv_data_cb) override;
  ISocket& Error(ErrorCb error_cb) override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;

  void Disconnect() override;

  ISocket& Connect(AddressPort const& destination,
                   ConnectedCb connected_cb) override;

  // LWIP RAW callbacks
  static void UdpClientRecv(void* arg, struct udp_pcb* upcb, struct pbuf* p,
                            const ip_addr_t* addr, u16_t port);

 protected:
  void OnError();

  ReadyToWriteCb ready_to_write_cb_;
  RecvDataCb recv_data_cb_;
  ErrorCb error_cb_;

  udp_pcb* pcb_{nullptr};
  ip_addr_t server_ipaddr_{};
  std::uint16_t server_port_{0};
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_UDP_SOCKET_H_
