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

#if AE_SUPPORT_TCP && LWIP_CB_SOCKET_ENABLED

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"
#  include "lwip/ip_addr.h"


#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace lwip_tcp_socket_internal {

}  // namespace lwip_tcp_socket_internal

LwipCBTcpSocket::LwipCBTcpSocket(IPoller& poller)
    : LwipCBSocket(poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1500 is our default MTU for TCP
  recv_buffer_.resize(1500);
}

int LwipCBTcpSocket::MakeSocket() {
  AE_TELED_DEBUG("MakeSocket");  

  return 0;
}

// Callback client data received
static err_t tcp_client_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    tcp_client_t *client = (tcp_client_t *)arg;

    if (p == NULL) {
        // Connection is closed
        client->connected = false;
        AE_TELED_DEBUG("Connection is closed");
        tcp_close(tpcb);
        return ERR_OK;
    }

    if (err != ERR_OK) {
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

    AE_TELED_DEBUG("Получено {} байт данных", copy_len);

    return ERR_OK;
}

// Callback client send
static err_t tcp_client_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    AE_TELED_DEBUG("Sended {} bytes", len);
    return ERR_OK;
}

// Callback for errors
static void tcp_client_error(void *arg, err_t err)
{
    tcp_client_t *client = (tcp_client_t *)arg;
    client->connected = false;
    AE_TELED_DEBUG("TCP error: {}", err);
}

static err_t tcp_client_connected(void * arg, struct tcp_pcb * tpcb, err_t err)
{
  tcp_client_t *client = (tcp_client_t *)arg;

    if (err != ERR_OK) {
        AE_TELED_ERROR("Connection error: {} {}", static_cast<int>(err), strerror(err));
        return err;
    }

    // Setting callbacks
    tcp_recv(tpcb, tcp_client_recv);
    tcp_sent(tpcb, tcp_client_sent);
    tcp_err(tpcb, tcp_client_error);

    client->connected = true;
    AE_TELED_DEBUG("Connected to the server");

    return ERR_OK;
}

ISocket& LwipCBTcpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {

  err_t err;
  ip_addr_t ipaddr;

  connected_cb_ = std::move(connected_cb);

  auto addr = GetSockAddr(destination);
  auto port = destination.port;

  this->tcp_client_.pcb = tcp_new();
  if(this->tcp_client_.pcb == nullptr) {
    AE_TELED_ERROR("MakeSocket error");
    this->tcp_client_.pcb = nullptr;
    return *this;
  }
  
  // Reuse address
  ip_set_option(this->tcp_client_.pcb, SOF_REUSEADDR);
  
  // Binding port
  /*err = tcp_bind(this->tcp_client_.pcb, IP_ADDR_ANY, port);
  if (err != ERR_OK) {
      AE_TELED_ERROR("Not binded: {} {}", static_cast<int>(err), strerror(err));
      tcp_close(this->tcp_client_.pcb);
      this->tcp_client_.pcb = nullptr;
      return *this;
  }
  AE_TELED_DEBUG("Port binded!");*/

  ipaddr = IPADDR4_INIT(addr.data.ipv4.sin_addr.s_addr);

  AE_TELED_ERROR("IP {}, port {}", addr.data.ipv4.sin_addr.s_addr, port);
  AE_TELED_ERROR("IP {}, port {}", ipaddr.u_addr.ip4.addr, port);

  // Setting callbacks
  tcp_arg(this->tcp_client_.pcb, &this->tcp_client_);
  tcp_err(this->tcp_client_.pcb, tcp_client_error);

  err = tcp_connect(this->tcp_client_.pcb, &ipaddr, port, tcp_client_connected);

  if(err != ERR_OK){
    AE_TELED_ERROR("Not connected {} {}", static_cast<int>(err), strerror(err));
    tcp_close(this->tcp_client_.pcb);
    this->tcp_client_.pcb = nullptr;
    return *this;
  }
  AE_TELED_DEBUG("Connected");

  /*defer[&]() {
    Poll();
    connected_cb_(connection_state_);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto addr = GetSockAddr(destination);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      connection_state_ = ConnectionState::kConnecting;
      return *this;
    }
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }*/

  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void LwipCBTcpSocket::OnConnectionEvent() {
  defer[&]() {
    if (connected_cb_) {
      connected_cb_(connection_state_);
    }
  };

  // check socket status
  auto sock_err = GetSocketError();
  if (!sock_err || (sock_err.value() != 0)) {
    AE_TELED_ERROR("Connect error {}, {}", sock_err,
                   strerror(sock_err.value_or(0)));
    connection_state_ = ConnectionState::kConnectionFailed;
    return;
  }

  AE_TELED_DEBUG("Socket connected");
  connection_state_ = ConnectionState::kConnected;
}
}  // namespace ae
#endif
