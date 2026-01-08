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

#include "aether/transport/system_sockets/sockets/lwip_udp_socket.h"

#if AE_SUPPORT_UDP && LWIP_SOCKET_ENABLED

#  include "lwip/sockets.h"

#  include "aether/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"

#  include "aether/tele/tele.h"

namespace ae {
LwipUdpSocket::LwipUdpSocket(IPoller& poller)
    : LwipSocket(poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1200 is our default MTU for UDP
  recv_buffer_.resize(1200);
}

int LwipUdpSocket::MakeSocket() {
  bool created = false;
  auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    AE_TELED_ERROR("LwIp UDP socket not created {}", strerror(errno));
    return kInvalidSocket;
  }

  // close the socket if not created
  defer[&] {
    if (!created) {
      close(sock);
    }
  };

  // make socket nonblocking
  if (lwip_fcntl(sock, F_SETFL, O_NONBLOCK) != ESP_OK) {
    AE_TELED_ERROR("lwip_fcntl set nonblocking mode error {} {}", errno,
                   strerror(errno));
    return kInvalidSocket;
  }

  AE_TELED_DEBUG("LwIp UDP socket created");
  created = true;
  return sock;
}

ISocket& LwipUdpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  // UDP connection means binding socket to a destination address
  assert((socket_ != kInvalidSocket) && "Socket is not initialized");

  defer[&]() {
    Poll();
    AE_TELED_DEBUG("LwIp UDP socket connectioin event {}", connection_state_);
    connected_cb(connection_state_);
  };

  auto lock = std::scoped_lock{socket_lock_};

  auto addr = GetSockAddr(destination);
  auto res = connect(socket_, addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state_ = ConnectionState::kConnected;
  return *this;
}
}  // namespace ae

#endif  // LWIP_SOCKET_ENABLED
