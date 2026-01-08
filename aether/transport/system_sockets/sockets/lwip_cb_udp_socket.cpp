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

#include "aether/transport/system_sockets/sockets/lwip_cb_udp_socket.h"

#if LWIP_CB_UDP_SOCKET_ENABLED
#  include "lwip/tcpip.h"

#  include "aether/transport/system_sockets/sockets/lwip_get_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipCBUdpSocket::LwipCBUdpSocket() = default;

LwipCBUdpSocket::~LwipCBUdpSocket() { Disconnect(); }

ISocket& LwipCBUdpSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket& LwipCBUdpSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket& LwipCBUdpSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBUdpSocket::Send(Span<std::uint8_t> data) {
  pbuf* p;
  err_t err;

  assert(pcb_ != nullptr && "Not connected to the server");

  LOCK_TCPIP_CORE();
  defer[] { UNLOCK_TCPIP_CORE(); };

  defer[&] {
    if (p != nullptr) {
      pbuf_free(p);
    }
  };

  p = pbuf_alloc(PBUF_TRANSPORT, data.size(), PBUF_RAM);
  if (!p) {
    AE_TELED_ERROR("Failed to allocate pbuf");
    OnError();
    return std::nullopt;
  }

  // Copying
  memcpy(p->payload, data.data(), data.size());

  err = udp_send(pcb_, p);
  if (err != ERR_OK) {
    AE_TELED_ERROR("Send failed: {}", err);
    OnError();
    return std::nullopt;
  }

  AE_TELED_ERROR("Sent {} bytes", data.size());

  return data.size();
}

void LwipCBUdpSocket::Disconnect() {
  if (pcb_ == nullptr) {
    return;
  }
  LOCK_TCPIP_CORE();
  udp_remove(pcb_);
  pcb_ = nullptr;
  UNLOCK_TCPIP_CORE();
}

ISocket& LwipCBUdpSocket::Connect(AddressPort const& destination,
                                  ConnectedCb connected_cb) {
  err_t err;
  auto connection_state = ConnectionState::kConnecting;
  defer[&] { connected_cb(connection_state); };

  LOCK_TCPIP_CORE();
  defer[] { UNLOCK_TCPIP_CORE(); };

  pcb_ = udp_new();
  if (pcb_ == nullptr) {
    AE_TELED_ERROR("Make UDP socket error");
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }
  auto on_failed = defer_at[&] {
    udp_remove(pcb_);
    pcb_ = nullptr;
  };

  // Reuse address
  ip_set_option(pcb_, SOF_REUSEADDR);

  auto lwip_adr = LwipGetAddr(destination);
  if (!lwip_adr) {
    AE_TELED_ERROR("Invalid address");
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  server_ipaddr_ = *lwip_adr;
  server_port_ = destination.port;

  // Receive callback
  udp_recv(pcb_, LwipCBUdpSocket::UdpClientRecv, this);

  err = udp_connect(pcb_, &server_ipaddr_, server_port_);
  if (err != ERR_OK) {
    AE_TELED_ERROR("Not connected: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  on_failed.Reset();
  connection_state = ConnectionState::kConnected;
  return *this;
}

// Callback client data received
void LwipCBUdpSocket::UdpClientRecv(void* arg, struct udp_pcb* upcb,
                                    struct pbuf* p, const ip_addr_t* addr,
                                    u16_t port) {
  auto* self = static_cast<LwipCBUdpSocket*>(arg);

  defer[&] {
    if (p != nullptr) {
      pbuf_free(p);
    }
  };

  if (p == nullptr) {
    AE_TELED_DEBUG("P is nullptr");
    return;
  }

  // Our server address?
  if (!ip_addr_cmp(&self->server_ipaddr_, addr)) {
    AE_TELED_ERROR("Received from unexpected IP:{}", ipaddr_ntoa(addr));
    self->OnError();
    return;
  }

  if (!self->recv_data_cb_) {
    return;
  }

  auto* packet = p;
  for (; packet != nullptr; packet = packet->next) {
    AE_TELED_DEBUG("Received {} bytes", packet->len);
    auto payload_span = Span<std::uint8_t>{
        static_cast<std::uint8_t*>(packet->payload),
        static_cast<std::size_t>(packet->len),
    };
    self->recv_data_cb_(payload_span);
  }
}

void LwipCBUdpSocket::OnError() {
  if (error_cb_) {
    error_cb_();
  }
}

}  // namespace ae
#endif
