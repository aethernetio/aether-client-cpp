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

#include "aether/client_connections/client_server_connection.h"

#include <chrono>

namespace ae {
ClientServerConnection::ClientServerConnection(
    ActionContext action_context, [[maybe_unused]] ObjPtr<Aether> const& aether,
    Server::ptr const& server, Channel::ptr const& channel,
    std::unique_ptr<ClientToServerStream> client_to_server_stream)
    : server_stream_{std::move(client_to_server_stream)},
      message_stream_dispatcher_{*server_stream_},
      ping_{action_context, server, channel, *server_stream_,
            std::chrono::milliseconds{AE_PING_INTERVAL_MS}},
#if defined TELEMETRY_ENABLED
      telemetry_{action_context, aether, *server_stream_},
#endif
      new_stream_event_sub_{
          message_stream_dispatcher_.new_stream_event().Subscribe(
              new_stream_event_, MethodPtr<&NewStreamEvent::Emit>{})},
      ping_error_sub_{ping_->ErrorEvent().Subscribe(
          [this](auto&&...) { server_error_event_.Emit(); })} {
}

ClientToServerStream& ClientServerConnection::server_stream() {
  return *server_stream_;
}

ByteIStream& ClientServerConnection::GetStream(Uid destination_uid) {
  return message_stream_dispatcher_.GetMessageStream(destination_uid);
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
  message_stream_dispatcher_.CloseStream(uid);
}

void ClientServerConnection::SendTelemetry() {
#if defined TELEMETRY_ENABLED
  telemetry_->SendTelemetry();
#endif
}

}  // namespace ae
