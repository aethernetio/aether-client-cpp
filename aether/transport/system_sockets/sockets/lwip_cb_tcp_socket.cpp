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
#include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#if LWIP_CB_TCP_SOCKET_ENABLED

#  include "aether/tele/tele.h"

namespace ae {
LwipCBTcpSocket::LwipCBTcpSocket() {
  cb_tcp_client.my_class = this;
}

LwipCBTcpSocket::~LwipCBTcpSocket() { Disconnect(); }

ISocket &LwipCBTcpSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket &LwipCBTcpSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket &LwipCBTcpSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBTcpSocket::Send(Span<std::uint8_t> data) {
  auto size_to_send = data.size();

  if (!cb_tcp_client.connected || cb_tcp_client.pcb == NULL) {
    AE_TELED_DEBUG("Not connected to the server");
    return std::nullopt;
  }

  err_t err =
      tcp_write(cb_tcp_client.pcb, data.data(), size_to_send, TCP_WRITE_FLAG_COPY);
  if (err != ERR_OK) {
    AE_TELED_ERROR("Sending error: {}, {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return std::nullopt;
    ;
  }

  // Start sending
  tcp_output(cb_tcp_client.pcb);

  return static_cast<std::size_t>(size_to_send);
}

void LwipCBTcpSocket::Disconnect() {
  if (cb_tcp_client.pcb != NULL) {
    tcp_close(cb_tcp_client.pcb);
    cb_tcp_client.pcb = NULL;
    cb_tcp_client.connected = false;
  }
}

ISocket& LwipCBTcpSocket::Connect(AddressPort const& destination,
                                  ConnectedCb connected_cb) {
  err_t err;
  ip_addr_t ipaddr;

  connected_cb_ = std::move(connected_cb);

  auto addr = GetSockAddr(destination);
  auto port = destination.port;

  connection_state_ = ConnectionState::kConnecting;

  this->cb_tcp_client.pcb = tcp_new();
  if (this->cb_tcp_client.pcb == nullptr) {
    AE_TELED_ERROR("MakeSocket TCP error");
    this->cb_tcp_client.pcb = nullptr;
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  // Reuse address
  ip_set_option(this->cb_tcp_client.pcb, SOF_REUSEADDR);

  ipaddr = IPADDR4_INIT(addr.data.ipv4.sin_addr.s_addr);

  AE_TELED_DEBUG("IP {}, port {}", addr.data.ipv4.sin_addr.s_addr, port);
  AE_TELED_DEBUG("IP {}, port {}", ipaddr.u_addr.ip4.addr, port);

  // Setting arguments
  tcp_arg(this->cb_tcp_client.pcb, &this->cb_tcp_client);
  // Setting callback
  tcp_err(this->cb_tcp_client.pcb, this->TcpClientError);

  err = tcp_connect(this->cb_tcp_client.pcb, &ipaddr, port, this->TcpClientConnected);

  if (err != ERR_OK) {
    AE_TELED_ERROR("Not connected: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    tcp_close(this->cb_tcp_client.pcb);
    this->cb_tcp_client.pcb = nullptr;
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }
  AE_TELED_DEBUG("Connect finished");

  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void LwipCBTcpSocket::OnConnectionEvent() {
  defer[&]() {
    if (connected_cb_) {
      connected_cb_(connection_state_);
    }
  };

  AE_TELED_DEBUG("Socket TCP connected");
  connection_state_ = ConnectionState::kConnected;
}

// Callback client data received
err_t LwipCBTcpSocket::TcpClientRecv(void *arg, struct tcp_pcb *tpcb,
                                  struct pbuf *p, err_t err) {
  CBTcpClient *client = static_cast<CBTcpClient *>(arg);

  defer[&] { pbuf_free(p); };

  if (p == NULL) {
    // Connection is closed
    client->connected = false;
    AE_TELED_DEBUG("Connection is closed");
    tcp_close(tpcb);
    return ERR_OK;
  }

  client->err = err;
  if (err != ERR_OK) {
    AE_TELED_ERROR("TCP recv error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return err;
  }

  auto payload_span =
      Span<std::uint8_t>{static_cast<std::uint8_t *>(p->payload), p->len};
  
  client->my_class->recv_data_cb_(payload_span);
  
  client->data_received = true;

  // Ack
  tcp_recved(tpcb, p->tot_len);

  AE_TELED_DEBUG("Received {} bytes", p->len);  

  return ERR_OK;
}

// Callback client send
err_t LwipCBTcpSocket::TcpClientSent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
  CBTcpClient *client = static_cast<CBTcpClient *>(arg);
  AE_TELED_DEBUG("Sended {} bytes", len);

  client->my_class->ready_to_write_cb_();
  return ERR_OK;
}

// Callback for errors
void LwipCBTcpSocket::TcpClientError(void *arg, err_t err) {
  CBTcpClient *client = static_cast<CBTcpClient *>(arg);

  client->err = err;
  if (err != ERR_OK) {
    AE_TELED_ERROR("TCP error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));

    client->connected = false;
    client->my_class->OnErrorEvent();
  }
}

err_t LwipCBTcpSocket::TcpClientConnected(void *arg, struct tcp_pcb *tpcb,
                                       err_t err) {
  CBTcpClient *client = static_cast<CBTcpClient *>(arg);

  client->err = err;
  if (err != ERR_OK) {
    AE_TELED_ERROR("Connection error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return err;
  }

  // Setting callbacks
  tcp_recv(tpcb, LwipCBTcpSocket::TcpClientRecv);
  tcp_sent(tpcb, LwipCBTcpSocket::TcpClientSent);
  tcp_err(tpcb, LwipCBTcpSocket::TcpClientError);

  client->connected = true;
  AE_TELED_DEBUG("Connected to the server");

  client->my_class->OnConnectionEvent();

  return ERR_OK;
}

void LwipCBTcpSocket::OnErrorEvent() {
  if (error_cb_) {
    error_cb_();
  }
}

std::optional<int> LwipCBTcpSocket::GetSocketError() {
  int err{};

  err = cb_tcp_client.err;
  cb_tcp_client.err = 0;
  AE_TELED_ERROR("Getsockopt error: {}, {}", static_cast<int>(err),
                 strerror(err_to_errno(err)));

  return err;
}

}  // namespace ae
#endif
