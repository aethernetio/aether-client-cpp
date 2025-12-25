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
#include "aether/types/small_function.h"

namespace ae {
/**
 * \brief Socket representation.
 * Socket implementation should create non blocking client sockets allowed to
 * send and receive data, with callback notifications.
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

  using ConnectedCb = SmallFunction<void(ConnectionState state)>;
  using ReadyToWriteCb = SmallFunction<void()>;
  using RecvDataCb = SmallFunction<void(Span<std::uint8_t> data_span)>;
  using ErrorCb = SmallFunction<void()>;

  virtual ~ISocket() = default;

  /**
   * \brief Set ready to write callback.
   */
  virtual ISocket& ReadyToWrite(ReadyToWriteCb ready_to_write_cb) = 0;
  /**
   * \brief Set recv data callback.
   */
  virtual ISocket& RecvData(RecvDataCb recv_data_cb) = 0;
  /**
   * \brief Set error callback.
   */
  virtual ISocket& Error(ErrorCb error_cb) = 0;
  /**
   * \brief Connect socket to the address.
   */
  virtual ISocket& Connect(AddressPort const& destination,
                           ConnectedCb connected_cb) = 0;
  /**
   * \brief Non blocking send data.
   * \return sent count, in case of error returns nullopt, if socket is busy
   * return 0.
   */
  virtual std::optional<std::size_t> Send(Span<std::uint8_t> data) = 0;
  /**
   * \brief Broke the connection.
   */
  virtual void Disconnect() = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_SOCKETS_ISOCKET_H_
