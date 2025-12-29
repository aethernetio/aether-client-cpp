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
#include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#if LWIP_CB_UDP_SOCKET_ENABLED

#  include "aether/tele/tele.h"

namespace ae {
LwipCBUdpSocket::LwipCBUdpSocket(IPoller & /*poller*/) {
  cb_udp_client.my_class = this;
}

LwipCBUdpSocket::~LwipCBUdpSocket() { Disconnect(); }

ISocket &LwipCBUdpSocket::ReadyToWrite(ReadyToWriteCb ready_to_write_cb) {
  ready_to_write_cb_ = std::move(ready_to_write_cb);
  return *this;
}

ISocket &LwipCBUdpSocket::RecvData(RecvDataCb recv_data_cb) {
  recv_data_cb_ = std::move(recv_data_cb);
  return *this;
}

ISocket &LwipCBUdpSocket::Error(ErrorCb error_cb) {
  error_cb_ = std::move(error_cb);
  return *this;
}

std::optional<std::size_t> LwipCBUdpSocket::Send(Span<std::uint8_t> data) {
  auto size_to_send = data.size();



  return static_cast<std::size_t>(size_to_send);
}

void LwipCBUdpSocket::Disconnect() {
  if (cb_udp_client.pcb != nullptr) {
    //udp_close(cb_udp_client.pcb);
    cb_udp_client.pcb = nullptr;
    cb_udp_client.connected = false;
  }
}

ISocket& LwipCBUdpSocket::Connect(AddressPort const& destination,
                                  ConnectedCb connected_cb) {
  err_t err;
  ip_addr_t ipaddr;

  connected_cb_ = std::move(connected_cb);

  auto addr = GetSockAddr(destination);
  auto port = destination.port;

  connection_state_ = ConnectionState::kConnecting;

  this->cb_udp_client.pcb = udp_new();
  if (this->cb_udp_client.pcb == nullptr) {
    AE_TELED_ERROR("MakeSocket UDP error");
    this->cb_udp_client.pcb = nullptr;
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  // Reuse address
  ip_set_option(this->cb_udp_client.pcb, SOF_REUSEADDR);

  ipaddr = IPADDR4_INIT(addr.data.ipv4.sin_addr.s_addr);

  AE_TELED_DEBUG("IP {}, port {}", addr.data.ipv4.sin_addr.s_addr, port);
  AE_TELED_DEBUG("IP {}, port {}", ipaddr.u_addr.ip4.addr, port);

  err= udp_connect(this->cb_udp_client.pcb, &ipaddr, port);

  if (err != ERR_OK) {
    AE_TELED_ERROR("Not connected: {} {}", static_cast<int>(err),
                   strerror(err_to_errno(err)));
    udp_disconnect(this->cb_udp_client.pcb);
    this->cb_udp_client.pcb = nullptr;
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }
  AE_TELED_DEBUG("Connect finished");

  connection_state_ = ConnectionState::kConnected;
  return *this;
}

void LwipCBUdpSocket::OnConnectionEvent() {
  defer[&]() {
    if (connected_cb_) {
      connected_cb_(connection_state_);
    }
  };

  AE_TELED_DEBUG("Socket UDP connected");
  connection_state_ = ConnectionState::kConnected;
}

// Callback client data received
err_t LwipCBUdpSocket::UdpClientRecv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
                               const ip_addr_t *addr, u16_t port) {
  

  return ERR_OK;
}

std::optional<int> LwipCBUdpSocket::GetSocketError() {
  int err{};

  err = cb_udp_client.err;
  cb_udp_client.err = 0;
  AE_TELED_ERROR("Getsockopt error: {}, {}", static_cast<int>(err),
                 strerror(err_to_errno(err)));

  return err;
}

}  // namespace ae
#endif
