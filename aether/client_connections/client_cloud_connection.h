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
#include <optional>

#include "aether/uid.h"
#include "aether/memory.h"
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
class Cloud;
/**
 * \brief The simplest cloud connection, which connects to one server a time.
 */
class ClientCloudConnection final : public ClientConnection {
  class ReconnectNotify : public NotifyAction<ReconnectNotify> {
    using NotifyAction<ReconnectNotify>::NotifyAction;
  };

 public:
  ClientCloudConnection(
      ActionContext action_context, ObjPtr<Cloud> const& cloud,
      std::unique_ptr<IServerConnectionFactory>&& server_connection_factory);

  void Connect() override;
  std::unique_ptr<ByteStream> CreateStream(Uid destination_uid,
                                           StreamId stream_id) override;
  NewStreamEvent::Subscriber new_stream_event() override;
  void CloseStream(Uid uid, StreamId stream_id) override;

  AE_REFLECT()

 private:
  void SelectConnection();

  void OnConnectionError();
  void NewStream(Uid uid, ByteStream& stream);

  ActionContext action_context_;
  ServerConnectionSelector server_connection_selector_;
  std::optional<AsyncForLoop<RcPtr<ClientServerConnection>>>
      connection_selector_loop_;

  // currently selected connection
  RcPtr<ClientServerConnection> server_connection_;

  NewStreamEvent new_stream_event_;
  Subscription new_stream_event_subscription_;
  MultiSubscription new_split_stream_subscription_;

  Subscription connection_status_subscription_;

  // known streams to clients
  std::map<Uid, std::unique_ptr<SplitterGate>> gates_;

  ReconnectNotify reconnect_notify_;
  Subscription reconnect_notify_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CLOUD_CONNECTION_H_
