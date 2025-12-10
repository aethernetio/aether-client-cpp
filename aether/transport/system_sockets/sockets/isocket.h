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

#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_ISOCKET_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_ISOCKET_H_

#include <cstdint>
#include <cstddef>

#include "aether/types/span.h"
#include "aether/types/address.h"
#include "aether/poller/poller_types.h"

namespace ae {
/**
 * \brief Socket representation.
 * Socket implementation should create non blocking client sockets with
 * functions connect, disconnect, send, receive.
 */
class ISocket {
 public:
  enum class ConnectionState : std::uint8_t {
    kNone,
    kConnecting,
    kConnected,
    kDisconnected,
    kConnectionFailed
  };

  virtual ~ISocket() = default;

  /**
   * \brief Casting to pollers DescriptorType
   */
  virtual explicit operator DescriptorType() const = 0;

  /**
   * \brief Establish a connection to the specified destination.
   * Non blocking method. If connection state is kConnecting wait until it's
   * finished.
   * \return connection state.
   */
  virtual ConnectionState Connect(AddressPort const& destination) = 0;
  /**
   * \brief Get current connection state.
   */
  virtual ConnectionState GetConnectionState() = 0;
  /**
   * \brief Broke the connection.
   */
  virtual void Disconnect() = 0;

  /**
   * \brief Non blocking send data.
   * \return sent count, in case of error returns nullopt, if socket is busy
   * return 0.
   */
  virtual std::optional<std::size_t> Send(Span<std::uint8_t> data) = 0;
  /**
   * \brief Non blocking receive data.
   * \return received count, in case of error returns nullopt, if socket is busy
   * return 0.
   */
  virtual std::optional<std::size_t> Receive(Span<std::uint8_t> data) = 0;

  /**
   * \brief Get the maximum packet size (MTU)
   */
  virtual std::size_t GetMaxPacketSize() const = 0;

  /**
   * \brief Know if socket is valid.
   */
  virtual bool IsValid() const = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_ISOCKET_H_
