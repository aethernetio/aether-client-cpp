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

#include "aether/tele/tele.h"

namespace ae {
MessageStreamDispatcher::MessageStreamDispatcher(
    ClientToServerStream& server_stream)
    : server_stream_{&server_stream},
      on_client_stream_subscription_{
          EventSubscriber{server_stream_->client_safe_api().send_message_event}
              .Subscribe(
                  *this,
                  MethodPtr<&MessageStreamDispatcher::OnSendMessage>{})} {}

MessageStreamDispatcher::NewStreamEvent::Subscriber
MessageStreamDispatcher::new_stream_event() {
  return new_stream_event_;
}

MessageStream& MessageStreamDispatcher::GetMessageStream(Uid uid) {
  auto stream_it = streams_.find(uid);
  if (stream_it != streams_.end()) {
    return *stream_it->second;
  }

  auto [new_it, ok] = streams_.emplace(uid, CreateMessageStream(uid));
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
    Uid uid) {
  auto message_stream = make_unique<MessageStream>(*server_stream_, uid);
  Tie(*message_stream, *server_stream_);
  return message_stream;
}

void MessageStreamDispatcher::OnSendMessage(Uid const& sender,
                                            DataBuffer const& data) {
  auto stream_it = streams_.find(sender);
  AE_TELED_DEBUG("received message from uid {}\ndata {}", sender, data);
  if (stream_it == std::end(streams_)) {
    AE_TELED_DEBUG("Stream not found create it {}", sender);
    stream_it =
        streams_.emplace_hint(stream_it, sender, CreateMessageStream(sender));
    new_stream_event_.Emit(sender, *stream_it->second);
  }

  // notify about new stream created
  stream_it->second->OnData(data);
}

}  // namespace ae
