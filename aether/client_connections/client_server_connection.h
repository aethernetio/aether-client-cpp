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

#include "aether/client_messages/message_stream_dispatcher.h"
#include "aether/client_connections/client_to_server_stream.h"

namespace ae {
/**
 * \brief Client's connection to a server for messages send.
 */
class ClientServerConnection {
 public:
  using NewStreamEvent = Event<void(Uid uid, Ptr<MessageStream> stream)>;

  explicit ClientServerConnection(
      Ptr<ClientToServerStream> client_to_server_stream);

  Ptr<ClientToServerStream> const& server_stream() const;

  EventSubscriber<void()> connected_event();
  EventSubscriber<void()> connection_error_event();
  ConnectionState connection_state() const;

  ByteStream& GetStream(Uid destination);
  NewStreamEvent::Subscriber new_stream_event();
  void CloseStream(Uid uid);

 private:
  Ptr<ClientToServerStream> server_stream_;
  Ptr<MessageStreamDispatcher> message_stream_dispatcher_;

  NewStreamEvent new_stream_event_;
  Subscription new_stream_event_subscription_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLIENT_SERVER_CONNECTION_H_
