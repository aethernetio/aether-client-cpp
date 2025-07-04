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

#ifndef AETHER_TRANSPORT_ITRANSPORT_H_
#define AETHER_TRANSPORT_ITRANSPORT_H_

#include "aether/events/events.h"
#include "aether/actions/action_view.h"

#include "aether/types/address.h"
#include "aether/transport/data_buffer.h"
#include "aether/transport/actions/packet_send_action.h"

namespace ae {
enum class ConnectionState : std::uint8_t {
  kUndefined,
  kDisconnected,
  kConnecting,
  kConnected,
};

struct ConnectionInfo {
  IpAddressPortProtocol destination;
  std::size_t max_packet_size;
  ConnectionState connection_state;
};

class ITransport {
 public:
  using DataReceiveEvent =
      Event<void(DataBuffer const& data, TimePoint current_time)>;
  using ConnectionSuccessEvent = Event<void()>;
  using ConnectionErrorEvent = Event<void()>;

  virtual ~ITransport() = default;

  virtual void Connect() = 0;
  virtual ConnectionInfo const& GetConnectionInfo() const = 0;
  virtual ConnectionSuccessEvent::Subscriber ConnectionSuccess() = 0;
  virtual ConnectionErrorEvent::Subscriber ConnectionError() = 0;

  virtual DataReceiveEvent::Subscriber ReceiveEvent() = 0;

  virtual ActionView<PacketSendAction> Send(DataBuffer data,
                                            TimePoint current_time) = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_ITRANSPORT_H_
