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
LwipUdpSocket::LwipUdpSocket(Ptr<IPoller> const& poller)
    : LwipSocket(*poller, MakeSocket()),
      connection_state_{ConnectionState::kNone} {
  // 1200 is our default MTU for UDP
  recv_buffer_.resize(1200);
}

int LwipUdpSocket::MakeSocket() noexcept {
  auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    AE_TELED_ERROR("LwIp UDP socket not created {}", strerror(errno));
    return kInvalidDescriptor;
  }

  // close the socket if not created
  auto on_failure = ae_defer_at[&] { close(sock); };

  // make socket nonblocking
  if (lwip_fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
    AE_TELED_ERROR("lwip_fcntl set nonblocking mode error {} {}", errno,
                   strerror(errno));
    return kInvalidDescriptor;
  }

  AE_TELED_DEBUG("LwIp UDP socket created");
  on_failure.Reset();
  return sock;
}

ISocket& LwipUdpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  // UDP connection means binding socket to a destination address
  assert(socket_ && "Socket is not initialized");

  ae_defer[&]() {
    AE_TELED_DEBUG("LwIp UDP socket connection event {}", connection_state_);
    connected_cb(connection_state_);
  };

  auto addr = GetSockAddr(destination);
  auto res =
      connect(*socket_->fd(), addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state_ = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state_ = ConnectionState::kConnected;
  Poll();
  return *this;
}
}  // namespace ae

#endif  // LWIP_SOCKET_ENABLED
