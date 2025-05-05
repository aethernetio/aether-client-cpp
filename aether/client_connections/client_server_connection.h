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

#ifndef AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
#define AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_

#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/actions/action_context.h"

#include "aether/memory.h"
#include "aether/server.h"
#include "aether/channel.h"
#include "aether/ae_actions/ping.h"
#include "aether/client_messages/message_stream_dispatcher.h"
#include "aether/client_connections/client_to_server_stream.h"

namespace ae {
/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
 public:
  using NewStreamEvent = Event<void(Uid uid, MessageStream& stream)>;

  explicit ClientServerConnection(
      ActionContext action_context, Server::ptr const& server,
      Channel::ptr const& channel,
      std::unique_ptr<ClientToServerStream> client_to_server_stream);

  ClientToServerStream& server_stream();

  ByteIStream& GetStream(Uid destination);
  NewStreamEvent::Subscriber new_stream_event();
  void CloseStream(Uid uid);

 private:
  std::unique_ptr<ClientToServerStream> server_stream_;
  std::unique_ptr<MessageStreamDispatcher> message_stream_dispatcher_;
  std::unique_ptr<Ping> ping_;
  NewStreamEvent new_stream_event_;
  Subscription new_stream_event_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
