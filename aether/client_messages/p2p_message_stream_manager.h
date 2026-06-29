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

#ifndef AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_MANAGER_H_
#define AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_MANAGER_H_

#include <map>
#include <memory>

#include "aether/ptr/ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/types/uid.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/cloud_connections/cloud_subscription.h"
#include "aether/client_messages/p2p_port_handle.h"

namespace ae {

class Client;

namespace p2p_stream_internal {
class P2pReceivePort;
}  // namespace p2p_stream_internal

class P2pMessageStreamManager {
 public:
  using NewPortEvent = Event<void(P2pPortHandle)>;

  P2pMessageStreamManager(AeContext const& ae_context,
                          Ptr<Client> const& client);

  P2pPortHandle CreatePort(Uid const& destination);
  NewPortEvent::Subscriber new_port_event();

 private:
  void NewMessageReceived(AeMessage const& message);
  void CleanUpPorts();

  using PortsMap =
      std::map<Uid, std::weak_ptr<p2p_stream_internal::P2pReceivePort>>;
  std::pair<std::shared_ptr<p2p_stream_internal::P2pReceivePort>, bool>
  GetOrCreatePort(Uid const& destination);

  AeContext ae_context_;
  PtrView<Client> client_;
  CloudServerConnections* cloud_connection_;
  PortsMap ports_;
  NewPortEvent new_port_event_;
  CloudEventListener on_message_received_sub_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_MANAGER_H_
