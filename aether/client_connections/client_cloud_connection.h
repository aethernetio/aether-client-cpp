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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_CLOUD_CONNECTION_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_CLOUD_CONNECTION_H_

#include <map>

#include "aether/uid.h"
#include "aether/async_for_loop.h"
#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/splitter_gate.h"

#include "aether/client_connections/client_connection.h"
#include "aether/client_connections/client_server_connection.h"
#include "aether/client_connections/server_connection_selector.h"

namespace ae {
class ClientCloudConnection final : public ClientConnection {
  class ReconnectNotify : public NotifyAction<ReconnectNotify> {
    using NotifyAction<ReconnectNotify>::NotifyAction;
  };

 public:
  explicit ClientCloudConnection(
      ActionContext action_context,
      Ptr<ServerConnectionSelector> client_server_connection_selector);

  void Connect() override;
  Ptr<ByteStream> CreateStream(Uid destination_uid,
                               StreamId stream_id) override;
  NewStreamEvent::Subscriber new_stream_event() override;
  void CloseStream(Uid uid, StreamId stream_id) override;

 private:
  void SelectConnection();

  void OnConnectionError();
  void NewStream(Uid uid, Ptr<ByteStream> stream);

  ActionContext action_context_;
  Ptr<ServerConnectionSelector> server_connection_selector_;
  Ptr<AsyncForLoop<Ptr<ClientServerConnection>>> connection_selector_loop_;

  // currently selected connection
  Ptr<ClientServerConnection> server_connection_;

  NewStreamEvent new_stream_event_;
  Subscription new_stream_event_subscription_;
  MultiSubscription new_split_stream_subscription_;

  Subscription connection_status_subscription_;

  // known streams to clients
  std::map<Uid, Ptr<SplitterGate>> gates_;

  ReconnectNotify reconnect_notify_;
  Subscription reconnect_notify_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CLOUD_CONNECTION_H_
