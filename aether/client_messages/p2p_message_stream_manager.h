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

#include "aether/ptr/ptr.h"
#include "aether/types/uid.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/ptr/ptr_view.h"
#include "aether/events/events.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/cloud_connections/cloud_subscription.h"

namespace ae {
class Client;
class P2pMessageStreamManager {
 public:
  using NewStreamEvent = Event<void(RcPtr<P2pStream>)>;

  P2pMessageStreamManager(ActionContext action_context,
                          Ptr<Client> const& client);

  RcPtr<P2pStream> CreateStream(Uid destination);
  NewStreamEvent::Subscriber new_stream_event();

 private:
  void NewMessageReceived(AeMessage const& message);
  void CleanUpStreams();
  RcPtr<P2pStream> MakeStream(Uid destination);

  void OnStreamUpdated(Uid destination);

  ActionContext action_context_;
  PtrView<Client> client_;
  ClientConnectionManager* connection_manager_;
  CloudServerConnections* cloud_connection_;
  std::map<Uid, RcPtrView<P2pStream>> streams_;
  NewStreamEvent new_stream_event_;
  CloudSubscription on_message_received_sub_;
  MultiSubscription message_stream_update_subs_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_MESSAGE_STREAM_MANAGER_H_
