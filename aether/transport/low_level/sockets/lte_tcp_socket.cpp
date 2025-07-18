/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/transport/low_level/sockets/lte_tcp_socket.h"

#if LTE_SOCKET_ENABLED

#  include "aether/tele/tele.h"

namespace ae {
namespace lte_socket_internal {
struct SockAddr {
  std::size_t size;
};

inline SockAddr GetSockAddr(IpAddressPort const& ip_address_port) {
  AE_TELED_DEBUG("Ip address port {}", ip_address_port);
}
}  // namespace lte_socket_internal

LteSocket::LteSocket(int socket) : socket_{socket} {}

LteSocket::~LteSocket() { Disconnect(); }

LteSocket::operator DescriptorType() const {
  return DescriptorType{static_cast<ae::DescriptorType::Socket> (socket_)};
}

LteSocket::ConnectionState LteSocket::Connect(
    IpAddressPort const& destination) {
  assert(socket_ != kInvalidSocket);
  AE_TELED_DEBUG("Destination {}", destination);
  return ConnectionState::kConnected;
}

LteSocket::ConnectionState LteSocket::GetConnectionState() {
  // check socket status


  return ConnectionState::kConnected;
}

void LteSocket::Disconnect() {
  if (socket_ == kInvalidSocket) {
    return;
  }

  socket_ = kInvalidSocket;
}

std::optional<std::size_t> LteSocket::Send(Span<std::uint8_t> data) {
  auto size_to_send = data.size();
  auto res = size_to_send;

  return static_cast<std::size_t>(res);
}

std::optional<std::size_t> LteSocket::Receive(Span<std::uint8_t> data) {
  auto res = 0;
  AE_TELED_DEBUG("Data {}", data);
  return static_cast<std::size_t>(res);
}

bool LteSocket::IsValid() const { return socket_ != kInvalidSocket; }

}  // namespace ae
#endif
