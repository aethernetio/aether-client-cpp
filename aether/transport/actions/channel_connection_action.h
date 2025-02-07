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

#ifndef AETHER_TRANSPORT_ACTIONS_CHANNEL_CONNECTION_ACTION_H_
#define AETHER_TRANSPORT_ACTIONS_CHANNEL_CONNECTION_ACTION_H_

#include <cstddef>

#include "aether/ptr/ptr.h"
#include "aether/address.h"
#include "aether/actions/action.h"

namespace ae {
class ITransport;

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

class ChannelConnectionAction : public Action<ChannelConnectionAction> {
 public:
  using Action::Action;

  virtual Ptr<ITransport> transport() const = 0;
  virtual ConnectionInfo connection_info() const = 0;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_ACTIONS_CHANNEL_CONNECTION_ACTION_H_
