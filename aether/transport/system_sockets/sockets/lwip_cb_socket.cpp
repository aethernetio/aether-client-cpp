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

#include "aether/transport/system_sockets/sockets/lwip_cb_socket.h"

#if LWIP_CB_SOCKET_ENABLED

#  include "aether/tele/tele.h"

namespace ae {
LwipCBSocket::LwipCBSocket(IPoller & /*poller*/, int /*socket*/) {
  cb_client.my_class = this;
}

LwipCBSocket::~LwipCBSocket() { Disconnect(); }

ISocket &LwipCBSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket &LwipCBSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket &LwipCBSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBSocket::Send(Span<std::uint8_t> data) {
  auto size_to_send = data.size();

  if (!cb_client.connected || cb_client.pcb == NULL) {
    AE_TELED_DEBUG("Not connected to the server");
    return std::nullopt;
    ;
  }

  err_t err =
      tcp_write(cb_client.pcb, data.data(), size_to_send, TCP_WRITE_FLAG_COPY);
  if (err != ERR_OK) {
    AE_TELED_ERROR("Sending error: {}, {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return std::nullopt;
    ;
  }

  // Start sending
  tcp_output(cb_client.pcb);

  return static_cast<std::size_t>(size_to_send);
}

void LwipCBSocket::Disconnect() {
  if (cb_client.pcb != NULL) {
    tcp_close(cb_client.pcb);
    cb_client.pcb = NULL;
    cb_client.connected = false;
  }
}

// Callback client data received
err_t LwipCBSocket::tcp_client_recv(void *arg, struct tcp_pcb *tpcb,
                                    struct pbuf *p, err_t err) {
  cb_client_t *client = (cb_client_t *)arg;

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
    pbuf_free(p);
    return err;
  }

  // Copy
  uint16_t copy_len = p->len;
  if (copy_len > sizeof(client->buffer) - client->buffer_len) {
    copy_len = sizeof(client->buffer) - client->buffer_len;
  }

  memcpy(client->buffer + client->buffer_len, p->payload, copy_len);
  client->buffer_len += copy_len;
  client->data_received = true;

  // Ack
  tcp_recved(tpcb, p->tot_len);
  pbuf_free(p);

  AE_TELED_DEBUG("Received {} bytes", copy_len);

  client->my_class->OnReadEvent();
  return ERR_OK;
}

// Callback client send
err_t LwipCBSocket::tcp_client_sent(void *arg, struct tcp_pcb *tpcb,
                                    u16_t len) {
  cb_client_t *client = (cb_client_t *)arg;
  AE_TELED_DEBUG("Sended {} bytes", len);

  client->my_class->OnWriteEvent();
  return ERR_OK;
}

// Callback for errors
void LwipCBSocket::tcp_client_error(void *arg, err_t err) {
  cb_client_t *client = (cb_client_t *)arg;

  client->err = err;
  if (err != ERR_OK) {
    AE_TELED_ERROR("TCP error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));

    client->connected = false;
    client->my_class->OnErrorEvent();
  }
}

err_t LwipCBSocket::tcp_client_connected(void *arg, struct tcp_pcb *tpcb,
                                         err_t err) {
  cb_client_t *client = (cb_client_t *)arg;

  client->err = err;
  if (err != ERR_OK) {
    AE_TELED_ERROR("Connection error: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    return err;
  }

  // Setting callbacks
  tcp_recv(tpcb, LwipCBSocket::tcp_client_recv);
  tcp_sent(tpcb, LwipCBSocket::tcp_client_sent);
  tcp_err(tpcb, LwipCBSocket::tcp_client_error);

  client->connected = true;
  AE_TELED_DEBUG("Connected to the server");

  client->my_class->OnConnectionEvent();

  return ERR_OK;
}

void LwipCBSocket::OnReadEvent() {
  auto buffer = Span{recv_buffer_.data(), recv_buffer_.size()};
  auto res = Receive(buffer);
  if (!res) {
    OnErrorEvent();
    return;
  }
  if (*res == 0) {
    // No data yet
    return;
  }
  buffer = buffer.sub(0, *res);
  if (recv_data_cb_) {
    recv_data_cb_(buffer);
  }
}

void LwipCBSocket::OnWriteEvent() {
  if (ready_to_write_cb_) {
    ready_to_write_cb_();
  }
}

void LwipCBSocket::OnErrorEvent() {
  if (error_cb_) {
    error_cb_();
  }
}

std::optional<std::size_t> LwipCBSocket::Receive(Span<std::uint8_t> buffer) {
  auto res = cb_client.buffer_len;

  if (cb_client.data_received) {
    std::memcpy(buffer.data(), cb_client.buffer, cb_client.buffer_len);

    cb_client.buffer_len = 0;
    cb_client.data_received = false;
  }

  return static_cast<std::size_t>(res);
}

std::optional<int> LwipCBSocket::GetSocketError() {
  int err{};

  err = cb_client.err;
  cb_client.err = 0;
  AE_TELED_ERROR("Getsockopt error: {}, {}", static_cast<int>(err),
                 strerror(err_to_errno(err)));

  return err;
}

}  // namespace ae
#endif
