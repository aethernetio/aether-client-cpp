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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_SOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_SOCKET_H_

#if (defined(ESP_PLATFORM))

#  define LWIP_SOCKET_ENABLED 1

#  include "aether/transport/system_sockets/sockets/isocket.h"

namespace ae {
class LwipSocket : public ISocket {
 public:
  static constexpr int kInvalidSocket = -1;

  explicit LwipSocket(int socket);
  ~LwipSocket() override;

  explicit operator DescriptorType() const override;
  ConnectionState Connect(IpAddressPort const& destination) override;
  ConnectionState GetConnectionState() override;
  void Disconnect() override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;
  std::optional<std::size_t> Receive(Span<std::uint8_t> data) override;
  bool IsValid() const override;

 private:
  int socket_;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_LWIP_SOCKET_H_
