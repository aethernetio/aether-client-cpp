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

#ifndef AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
#define AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_

#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_context.h"

#include "aether/common.h"
#include "aether/memory.h"
#include "aether/server.h"
#include "aether/channel.h"
#include "aether/ae_actions/ping.h"
#include "aether/actions/action_ptr.h"
#include "aether/ae_actions/telemetry.h"
#include "aether/types/async_for_loop.h"
#include "aether/client_messages/message_stream_dispatcher.h"
#include "aether/server_connections/server_channel_stream.h"
#include "aether/server_connections/client_to_server_stream.h"

namespace ae {
class Aether;
class Client;

/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
 public:
  using NewStreamEvent = Event<void(Uid uid, MessageStream& stream)>;
  using ServerErrorEvent = Event<void()>;

  explicit ClientServerConnection(ActionContext action_context,
                                  ObjPtr<Aether> const& aether,
                                  ObjPtr<Client> const& client,
                                  Server::ptr const& server);

  AE_CLASS_NO_COPY_MOVE(ClientServerConnection)

  ClientToServerStream& server_stream();

  ByteIStream& GetStream(Uid destination);
  NewStreamEvent::Subscriber new_stream_event();
  ServerErrorEvent::Subscriber server_error_event();
  void CloseStream(Uid uid);

  void SendTelemetry();

  void NextChannel();

 private:
  void PingError();

  ActionContext action_context_;
  PtrView<Client> client_;
  PtrView<Server> server_;
  std::unique_ptr<MessageStreamDispatcher> message_stream_dispatcher_;
  OwnActionPtr<Ping> ping_;
#if defined TELEMETRY_ENABLED
  OwnActionPtr<Telemetry> telemetry_;
#endif
  std::optional<AsyncForLoop<Channel::ptr>> channel_loop_;
  std::size_t selected_channel_index_;
  std::unique_ptr<ServerChannelStream> server_channel_stream_;
  std::unique_ptr<ClientToServerStream> client_to_server_stream_;

  NewStreamEvent new_stream_event_;
  ServerErrorEvent server_error_event_;
  Subscription new_stream_event_sub_;
  Subscription ping_error_sub_;
};
}  // namespace ae

#endif  // AETHER_SERVER_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
