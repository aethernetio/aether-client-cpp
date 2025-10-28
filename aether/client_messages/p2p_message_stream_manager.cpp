/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/client_messages/p2p_message_stream_manager.h"

#include "aether/client.h"

namespace ae {
P2pMessageStreamManager::P2pMessageStreamManager(ActionContext action_context,
                                                 ObjPtr<Client> const& client)
    : action_context_{action_context},
      client_{client},
      connection_manager_{&client->connection_manager()},
      client_stream_{&client->cloud_message_stream()} {
  on_message_received_ = client_stream_->out_data_event().Subscribe(
      *this, MethodPtr<&P2pMessageStreamManager::NewMessageReceived>{});
}

RcPtr<P2pStream> P2pMessageStreamManager::CreateStream(Uid destination) {
  CleanUpStreams();
  auto it = streams_.find(destination);
  if (it != std::end(streams_)) {
    // after cleanup only used streams should stay
    assert(it->second);
    return it->second.lock();
  }
  // create new stream
  auto stream = MakeStream(destination);
  streams_.emplace(destination, stream);
  return stream;
}

P2pMessageStreamManager::NewStreamEvent::Subscriber
P2pMessageStreamManager::new_stream_event() {
  return EventSubscriber{new_stream_event_};
}

void P2pMessageStreamManager::NewMessageReceived(AeMessage const& message) {
  auto it = streams_.find(message.uid);
  if (it == std::end(streams_) || !it->second) {
    auto stream = CreateStream(message.uid);
    new_stream_event_.Emit(stream);
    // write out first data
    stream->WriteOut(message.data);
  }
}

void P2pMessageStreamManager::CleanUpStreams() {
  // remove unused streams
  for (auto it = streams_.begin(); it != streams_.end();) {
    if (!it->second) {
      it = streams_.erase(it);
    } else {
      ++it;
    }
  }
}

RcPtr<P2pStream> P2pMessageStreamManager::MakeStream(Uid destination) {
  Client::ptr client_ptr = client_.Lock();
  assert(client_ptr);
  return MakeRcPtr<P2pStream>(action_context_, client_ptr, destination);
}

}  // namespace ae
