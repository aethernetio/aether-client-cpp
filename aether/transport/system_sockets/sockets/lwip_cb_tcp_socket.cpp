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

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
namespace lwip_tcp_socket_internal {}  // namespace lwip_tcp_socket_internal

LwipCBTcpSocket::LwipCBTcpSocket(IPoller& poller)
    : LwipCBSocket(poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1500 is our default MTU for TCP
  recv_buffer_.resize(1500);
}

int LwipCBTcpSocket::MakeSocket(){
  AE_TELED_DEBUG("MakeSocket");

  return 0;
}

ISocket& LwipCBTcpSocket::Connect(AddressPort const& destination,
                                  ConnectedCb connected_cb) {
  err_t err;
  ip_addr_t ipaddr;

  connected_cb_ = std::move(connected_cb);

  auto addr = GetSockAddr(destination);
  auto port = destination.port;

  connection_state_ = ConnectionState::kConnecting;

  this->cb_client.pcb = tcp_new();
  if (this->cb_client.pcb == nullptr) {
    AE_TELED_ERROR("MakeSocket error");
    this->cb_client.pcb = nullptr;
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  // Reuse address
  ip_set_option(this->cb_client.pcb, SOF_REUSEADDR);

  ipaddr = IPADDR4_INIT(addr.data.ipv4.sin_addr.s_addr);

  AE_TELED_DEBUG("IP {}, port {}", addr.data.ipv4.sin_addr.s_addr, port);
  AE_TELED_DEBUG("IP {}, port {}", ipaddr.u_addr.ip4.addr, port);

  // Setting arguments
  tcp_arg(this->cb_client.pcb, &this->cb_client);
  // Setting callback
  tcp_err(this->cb_client.pcb, this->TcpClientError);

  err = tcp_connect(this->cb_client.pcb, &ipaddr, port, this->TcpClientConnected);

  if (err != ERR_OK) {
    AE_TELED_ERROR("Not connected: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    tcp_close(this->cb_client.pcb);
    this->cb_client.pcb = nullptr;
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

  AE_TELED_DEBUG("Socket connected");
  connection_state_ = ConnectionState::kConnected;
}
}  // namespace ae
#endif
