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

#include "aether/memory.h"
#include "aether/types/uid.h"
#include "aether/actions/timer_action.h"
#include "aether/events/event_subscription.h"

#include "aether/stream_api/istream.h"

#include "aether/client_connections/client_connection.h"
#include "aether/connection_manager/iserver_connection_pool.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/connection_manager/client_connection_manager.h"

namespace ae {
/**
 * \brief The simplest cloud connection, which connects to one server a time.
 */
class ClientCloudConnection final : public ClientConnection,
                                    public IServerConnectionPool {
  using NextServerLoopTimer = TimerAction;

 public:
  ClientCloudConnection(ActionContext action_context,
                        ClientConnectionManager& connection_manager);

  std::unique_ptr<ByteIStream> CreateStream(Uid destination_uid) override;
  NewStreamEvent::Subscriber new_stream_event() override;
  void CloseStream(Uid uid) override;
  void SendTelemetry() override;

  RcPtr<ClientServerConnection> TopConnection() override;
  bool Rotate() override;

  AE_REFLECT()

 private:
  void SubscribeToMessageStreams();

  void NewStream(Uid uid, ByteIStream& stream);

  ActionContext action_context_;
  ClientConnectionManager* connection_manager_;

  NewStreamEvent new_stream_event_;
  Subscription new_stream_event_subscription_;

  Subscription server_error_sub_;

  // known streams to clients
  std::map<Uid, std::unique_ptr<ByteStream>> streams_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_CLOUD_CONNECTION_H_
