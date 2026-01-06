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

#include "aether/transport/system_sockets/sockets/lwip_cb_tcp_socket.h"

#if LWIP_CB_TCP_SOCKET_ENABLED
#  include "lwip/tcpip.h"

#  include "aether/transport/system_sockets/sockets/lwip_get_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipCBTcpSocket::LwipCBTcpSocket() = default;

LwipCBTcpSocket::~LwipCBTcpSocket() { Disconnect(); }

ISocket& LwipCBTcpSocket::ReadyToWrite(
    [[maybe_unused]] ReadyToWriteCb ready_to_write_cb) {
  return *this;
}

ISocket& LwipCBTcpSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket& LwipCBTcpSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBTcpSocket::Send(Span<std::uint8_t> data) {
  assert((pcb_ != nullptr) && "Not connected to the server");

  LOCK_TCPIP_CORE();
  defer[&] { UNLOCK_TCPIP_CORE(); };

  err_t err = tcp_write(pcb_, data.data(), static_cast<u16_t>(data.size()),
                        TCP_WRITE_FLAG_COPY);
  if (err != ERR_OK) {
    if (err == ERR_MEM) {
      // internal buffer is full try resend data later
      return 0;
    }
    AE_TELED_ERROR("Sending error: {}, {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return std::nullopt;
  }

  // Start sending
  tcp_output(pcb_);

  return data.size();
}

void LwipCBTcpSocket::Disconnect() {
  if (pcb_ == nullptr) {
    return;
  }

  LOCK_TCPIP_CORE();
  tcp_close(pcb_);
  UNLOCK_TCPIP_CORE();
  pcb_ = nullptr;
}

ISocket& LwipCBTcpSocket::Connect(AddressPort const& destination,
                                  ConnectedCb connected_cb) {
  err_t err;

  connected_cb_ = std::move(connected_cb);
  connection_state_ = ConnectionState::kConnecting;

  defer[&] { OnConnectionEvent(); };

  LOCK_TCPIP_CORE();
  defer[] { UNLOCK_TCPIP_CORE(); };

  pcb_ = tcp_new();
  if (pcb_ == nullptr) {
    AE_TELED_ERROR("Make tcp socket error");
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  auto on_failed = defer_at[&] {
    tcp_close(pcb_);
    pcb_ = nullptr;
  };

  // Reuse address
  ip_set_option(pcb_, SOF_REUSEADDR);

  auto lwip_adr = LwipGetAddr(destination);
  if (!lwip_adr) {
    AE_TELED_ERROR("Make lwip address error");
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  // Setting arguments
  tcp_arg(pcb_, this);
  // Setting callback
  tcp_err(pcb_, LwipCBTcpSocket::TcpClientError);

  err = tcp_connect(pcb_, &*lwip_adr, destination.port,
                    LwipCBTcpSocket::TcpClientConnected);

  if (err != ERR_OK) {
    AE_TELED_ERROR("Not connected: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  on_failed.Reset();
  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void LwipCBTcpSocket::OnConnectionEvent() {
  AE_TELED_DEBUG("LwIp CB TCP socket connectioin event {}", connection_state_);

  if (connected_cb_) {
    connected_cb_(connection_state_);
  }
}

// Callback client data received
err_t LwipCBTcpSocket::TcpClientRecv(void* arg, struct tcp_pcb* tpcb,
                                     struct pbuf* p, err_t err) {
  auto* self = static_cast<LwipCBTcpSocket*>(arg);

  defer[&] {
    if (p != nullptr) {
      pbuf_free(p);
    }
  };

  if (p == nullptr) {
    // Connection is closed
    AE_TELED_DEBUG("Connection is closed");
    return ERR_OK;
  }

  if (err != ERR_OK) {
    AE_TELED_ERROR("TCP recv error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    self->OnErrorEvent();
    return err;
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

  // Ack
  tcp_recved(tpcb, p->tot_len);

  return ERR_OK;
}

// Callback for errors
void LwipCBTcpSocket::TcpClientError(void* arg, err_t err) {
  auto* self = static_cast<LwipCBTcpSocket*>(arg);
  if (err != ERR_OK) {
    AE_TELED_ERROR("TCP error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    self->OnErrorEvent();
  }
}

err_t LwipCBTcpSocket::TcpClientConnected(void* arg, struct tcp_pcb* tpcb,
                                          err_t err) {
  auto* self = static_cast<LwipCBTcpSocket*>(arg);
  if (err != ERR_OK) {
    AE_TELED_ERROR("Connection error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    self->OnErrorEvent();
    return err;
  }

  // Setting callbacks
  tcp_recv(tpcb, LwipCBTcpSocket::TcpClientRecv);
  tcp_err(tpcb, LwipCBTcpSocket::TcpClientError);

  AE_TELED_DEBUG("Connected to the server");

  self->OnConnectionEvent();
  return ERR_OK;
}

void LwipCBTcpSocket::OnErrorEvent() {
  if (error_cb_) {
    error_cb_();
  }
}
}  // namespace ae
#endif
