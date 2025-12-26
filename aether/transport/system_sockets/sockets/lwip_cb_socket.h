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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_SOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_SOCKET_H_

#if (defined(ESP_PLATFORM))

#  define LWIP_CB_SOCKET_ENABLED 1

#  include "lwip/tcp.h"
#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"
#  include "lwip/ip_addr.h"

#  include "aether/poller/poller.h"
#  include "aether/types/data_buffer.h"
#  include "aether/events/event_subscription.h"
#  include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
class LwipCBSocket;

typedef struct {
  LwipCBSocket *my_class;
  struct tcp_pcb *pcb;
  uint8_t buffer[1500];
  uint16_t buffer_len;
  bool connected;
  bool data_received;
  err_t err;
} cb_client_t;

/**
 * \brief Base implementation of LWIP socket.
 */
class LwipCBSocket : public ISocket {
 public:
  static constexpr int kInvalidSocket = -1;

  explicit LwipCBSocket(IPoller &poller, int socket);
  ~LwipCBSocket() override;

  ISocket &ReadyToWrite(ReadyToWriteCb ready_to_write_cb) override;
  ISocket &RecvData(RecvDataCb recv_data_cb) override;
  ISocket &Error(ErrorCb error_cb) override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;

  void Disconnect() override;

  virtual void OnConnectionEvent() = 0;

  // LWIP RAW callbacks
  static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                               err_t err);
  static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
  static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err);
  static void tcp_client_error(void *arg, err_t err);

  cb_client_t cb_client{};

 protected:
  void OnReadEvent();
  void OnWriteEvent();
  void OnErrorEvent();

  std::optional<std::size_t> Receive(Span<std::uint8_t> buffer);

  std::optional<int> GetSocketError();

  ReadyToWriteCb ready_to_write_cb_;
  RecvDataCb recv_data_cb_;
  ErrorCb error_cb_;

  DataBuffer recv_buffer_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_CB_SOCKET_H_
