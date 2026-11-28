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

#include "aether/transport/system_sockets/sockets/unix_udp_socket.h"

#if AE_SUPPORT_UDP && UNIX_SOCKET_ENABLED
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <netinet/udp.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <unistd.h>

#  include <cerrno>

#  include "aether-miscpp/misc/defer.h"
#  include "aether/transport/system_sockets/sockets/get_sock_addr.h"
#  include "aether/transport/system_sockets/udp/udp_datagram_limits.h"

#  include "aether/tele.h"

namespace ae {
UnixUdpSocket::UnixUdpSocket(Ptr<IPoller> const& poller)
    : UnixSocket{*poller, MakeSocket()} {
  recv_buffer_.resize(kCloudUdpReceiveBufferBytes);
}

int UnixUdpSocket::MakeSocket() {
  bool created = false;
  auto sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock == kInvalidDescriptor) {
    AETHER_TELED_ERROR("Socket creation error {} {}", errno, strerror(errno));
    return kInvalidDescriptor;
  }

  // close socket on error
  ae_defer[&] {
    if (!created) {
      close(sock);
    }
  };

#  if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DO)
  int const dont_fragment = IP_PMTUDISC_DO;
  if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &dont_fragment,
                 sizeof(dont_fragment)) != 0) {
    AETHER_TELED_ERROR("Cannot disable UDP fragmentation {}", errno);
    return kInvalidDescriptor;
  }
#  elif defined(IP_DONTFRAG)
  int const dont_fragment = 1;
  if (setsockopt(sock, IPPROTO_IP, IP_DONTFRAG, &dont_fragment,
                 sizeof(dont_fragment)) != 0) {
    AETHER_TELED_ERROR("Cannot disable UDP fragmentation {}", errno);
    return kInvalidDescriptor;
  }
#  endif

  int send_buffer = AE_SOCKET_SEND_BUFFER_BYTES;
  if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buffer,
                 sizeof(send_buffer)) != 0) {
    AETHER_TELED_ERROR("Cannot set UDP send buffer {}", errno);
    return kInvalidDescriptor;
  }
  // make socket non-blocking
  if (fcntl(sock, F_SETFL, O_NONBLOCK) != 0) {
    AETHER_TELED_ERROR("Socket set O_NONBLOCK error {} {}", errno,
                       strerror(errno));
    return kInvalidDescriptor;
  }
  created = true;
  return sock;
}

ISocket& UnixUdpSocket::Connect(AddressPort const& destination,
                                ConnectedCb connected_cb) {
  // UDP connection means binding socket to a destination address
  assert(socket_.has_value() && "Socket is not initialized");
  ConnectionState connection_state{ConnectionState::kNone};
  ae_defer[&]() {
    Poll();
    connected_cb(connection_state);
  };

  auto addr = GetSockAddr(destination);
  auto res =
      connect(*socket_->fd(), addr.addr(), static_cast<socklen_t>(addr.size));
  if (res == -1) {
    AETHER_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    connection_state = ConnectionState::kConnectionFailed;
    return *this;
  }

  connection_state = ConnectionState::kConnected;
  return *this;
}

std::optional<std::size_t> UnixUdpSocket::Receive(DescriptorType fd,
                                                 Span<std::uint8_t> buffer) {
  iovec vec{buffer.data(), buffer.size()};
  for (;;) {
    msghdr msg{};
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;
    auto const size = recvmsg(fd, &msg, 0);
    if (size < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
      }
      AETHER_TELED_ERROR("UDP recv error {} {}", errno, strerror(errno));
      return std::nullopt;
    }
    // Discard a legacy oversized datagram as a unit. Keep draining the
    // socket so a following complete datagram still reaches the decoder.
    if (size == 0 || (msg.msg_flags & MSG_TRUNC) != 0) {
      continue;
    }
    return static_cast<std::size_t>(size);
  }
}

}  // namespace ae
#endif
