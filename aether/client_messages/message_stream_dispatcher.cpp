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

#include "aether/client_messages/message_stream_dispatcher.h"

#include <utility>

#include "aether/methods/client_api/client_safe_api.h"

namespace ae {
MessageStreamDispatcher::MessageStreamDispatcher(ByteStream& connection_stream)
    : protocol_read_gate_{protocol_context_, ClientSafeApi{}} {
  Tie(protocol_read_gate_, connection_stream);
  on_client_stream_subscription_ =
      protocol_context_.MessageEvent<ClientSafeApi::StreamToClient>().Subscribe(
          *this, MethodPtr<&MessageStreamDispatcher::OnStreamToClient>{});
}

MessageStreamDispatcher::NewStreamEvent::Subscriber
MessageStreamDispatcher::new_stream_event() {
  return new_stream_event_;
}

MessageStream& MessageStreamDispatcher::GetMessageStream(Uid uid) {
  auto stream_it = streams_.find(uid);
  if (stream_it != streams_.end()) {
    return *stream_it->second;
  }

  auto [new_it, ok] = streams_.emplace(
      uid,
      CreateMessageStream(uid, StreamIdGenerator::GetNextClientStreamId()));
  assert(ok);
  return *new_it->second;
}

void MessageStreamDispatcher::CloseStream(Uid uid) {
  auto stream_it = streams_.find(uid);
  if (stream_it != streams_.end()) {
    streams_.erase(stream_it);
  }
}

std::unique_ptr<MessageStream> MessageStreamDispatcher::CreateMessageStream(
    Uid uid, StreamId stream_id) {
  auto message_stream =
      make_unique<MessageStream>(protocol_context_, uid, stream_id);
  Tie(*message_stream, protocol_read_gate_);
  return message_stream;
}

void MessageStreamDispatcher::OnStreamToClient(
    MessageEventData<ClientSafeApi::StreamToClient> const& msg) {
  auto const& message = msg.message();
  auto stream_it = streams_.find(message.uid);
  if (stream_it != streams_.end()) {
    if (stream_it->second->stream_id() != message.stream_id) {
      stream_it->second->set_stream_id(message.stream_id);
    }
    return;
  }

  stream_it = streams_.emplace_hint(
      stream_it, message.uid,
      CreateMessageStream(message.uid, message.stream_id));

  // notify about new stream created
  new_stream_event_.Emit(message.uid, *stream_it->second);
}

}  // namespace ae
