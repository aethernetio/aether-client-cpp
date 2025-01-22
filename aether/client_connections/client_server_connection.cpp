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

namespace ae {
ClientServerConnection::ClientServerConnection(
    Ptr<ClientToServerStream> client_to_server_stream)
    : server_stream_{std::move(client_to_server_stream)},
      message_stream_dispatcher_{
          MakePtr<MessageStreamDispatcher>(*server_stream_)} {
  new_stream_event_subscription_ =
      message_stream_dispatcher_->new_stream_event().Subscribe(
          [this](auto uid, auto stream) {
            new_stream_event_.Emit(uid, std::move(stream));
          });
}

Ptr<ClientToServerStream> const& ClientServerConnection::server_stream() const {
  return server_stream_;
}

ByteStream& ClientServerConnection::GetStream(Uid destination_uid) {
  return message_stream_dispatcher_->GetMessageStream(destination_uid);
}

ClientServerConnection::NewStreamEvent::Subscriber
ClientServerConnection::new_stream_event() {
  return new_stream_event_;
}

void ClientServerConnection::CloseStream(Uid uid) {
  message_stream_dispatcher_->CloseStream(uid);
}

}  // namespace ae
