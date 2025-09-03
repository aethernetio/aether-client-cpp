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

#include <chrono>

namespace ae {
ClientServerConnection::ClientServerConnection(
    ActionContext action_context, [[maybe_unused]] ObjPtr<Aether> const& aether,
    ObjPtr<Client> const& client, Server::ptr const& server)
    : action_context_{action_context},
      client_{client},
      server_{server},
      selected_channel_index_{0} {
  client_to_server_stream_ = std::make_unique<ClientToServerStream>(
      action_context, client, server->server_id);

  channel_loop_.emplace([&]() { selected_channel_index_ = 0; },
                        [&]() {
                          auto server_ptr = server_.Lock();
                          assert(server_ptr);
                          return selected_channel_index_ <
                                 server_ptr->channels.size();
                        },
                        [&]() { selected_channel_index_++; },
                        [&]() {
                          auto server_ptr = server_.Lock();
                          assert(server_ptr);
                          return server_ptr->channels[selected_channel_index_];
                        });

  message_stream_dispatcher_ =
      std::make_unique<MessageStreamDispatcher>(*client_to_server_stream_);
  new_stream_event_sub_ =
      message_stream_dispatcher_->new_stream_event().Subscribe(
          new_stream_event_, MethodPtr<&NewStreamEvent::Emit>{});

#if defined TELEMETRY_ENABLED
  telemetry_ = OwnActionPtr<Telemetry>{action_context, aether,
                                       *client_to_server_stream_};
#endif
}

ClientToServerStream& ClientServerConnection::server_stream() {
  return *client_to_server_stream_;
}

ByteIStream& ClientServerConnection::GetStream(Uid destination_uid) {
  return message_stream_dispatcher_->GetMessageStream(destination_uid);
}

ClientServerConnection::NewStreamEvent::Subscriber
ClientServerConnection::new_stream_event() {
  return new_stream_event_;
}

ClientServerConnection::ServerErrorEvent::Subscriber
ClientServerConnection::server_error_event() {
  return server_error_event_;
}

void ClientServerConnection::CloseStream(Uid uid) {
  message_stream_dispatcher_->CloseStream(uid);
}

void ClientServerConnection::SendTelemetry() {
#if defined TELEMETRY_ENABLED
  telemetry_->SendTelemetry();
#endif
}

void ClientServerConnection::NextChannel() {
  auto server_ptr = server_.Lock();
  if (!server_ptr) {
    server_error_event_.Emit();
    return;
  }

  auto channel = channel_loop_->Update();
  if (!channel) {
    server_error_event_.Emit();
    return;
  }

  // TODO: add handle link and unlink events
  server_channel_stream_ =
      std::make_unique<ServerChannelStream>(action_context_, channel);
  Tie(*client_to_server_stream_, *server_channel_stream_);

  // Ping action should be created for channel personally
  ping_ = OwnActionPtr<Ping>{action_context_, server_ptr, channel,
                             *client_to_server_stream_,
                             std::chrono::milliseconds{AE_PING_INTERVAL_MS}},
  ping_error_sub_ =
      ping_->StatusEvent().Subscribe(OnError{[this]() { PingError(); }});
}

void ClientServerConnection::PingError() {
  // TODO: handle ping error depends on connection open status
  server_error_event_.Emit();
}

}  // namespace ae
