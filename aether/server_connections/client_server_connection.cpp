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

#include "aether/server_connections/client_server_connection.h"

#include "aether/server.h"
#include "aether/server_connections/server_channel.h"

#include "aether/tele/tele.h"

namespace ae {
static constexpr std::size_t kBufferCapacity = 200;

ClientServerConnection::ClientServerConnection(ActionContext action_context,
                                               ObjPtr<Aether> const& aether,
                                               ObjPtr<Client> const& client,
                                               Server::ptr const& server)
    : action_context_{action_context},
      client_{client},
      server_{server},
      channel_manager_{action_context_, aether, server},
      channel_select_stream_{action_context_, channel_manager_},
      buffer_stream_{action_context_, kBufferCapacity},
      client_to_server_stream_{action_context, client, server->server_id} {
  AE_TELED_DEBUG("Client server connection");
  Tie(client_to_server_stream_, buffer_stream_, channel_select_stream_);
  StreamUpdate();
  SubscribeToSelectChannel();
}

ClientToServerStream& ClientServerConnection::server_stream() {
  return client_to_server_stream_;
}

void ClientServerConnection::SubscribeToSelectChannel() {
  stream_update_sub_ = channel_select_stream_.stream_update_event().Subscribe(
      *this, MethodPtr<&ClientServerConnection::StreamUpdate>{});
}

void ClientServerConnection::StreamUpdate() {
  auto const* channel = channel_select_stream_.server_channel();
  // check if channel is updated
  if (server_channel_ == channel) {
    return;
  }
  server_channel_ = channel;

  Server::ptr server = server_.Lock();
  assert(server);
  // Create new ping if channel is updated
  // TODO: add ping interval config
  ping_ =
      OwnActionPtr<Ping>{action_context_, server, server_channel_->channel(),
                         client_to_server_stream_, std::chrono::seconds{5}};
}

}  // namespace ae
