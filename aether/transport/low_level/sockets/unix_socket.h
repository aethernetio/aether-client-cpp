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

#ifndef AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_UNIX_SOCKET_H_
#define AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_UNIX_SOCKET_H_

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
    defined(__FreeBSD__)

#  define UNIX_SOCKET_ENABLED 1

#  include "aether/transport/low_level/sockets/isocket.h"

namespace ae {
/**
 * \brief Base implementation of unix socket.
 */
class UnixSocket : public ISocket {
 public:
  static constexpr int kInvalidSocket = -1;

  explicit UnixSocket(int socket);
  ~UnixSocket() override;

  explicit operator DescriptorType() const override;
  ConnectionState Connect(IpAddressPort const& destination) override;
  ConnectionState GetConnectionState() override;
  void Disconnect() override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;
  std::optional<std::size_t> Receive(Span<std::uint8_t> data) override;
  bool IsValid() const override;

 protected:
  int socket_;
};
}  // namespace ae

#endif

#endif  // AETHER_TRANSPORT_LOW_LEVEL_SOCKETS_UNIX_SOCKET_H_
