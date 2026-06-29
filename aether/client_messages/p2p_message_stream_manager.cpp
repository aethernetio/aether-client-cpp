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
#include "aether/work_cloud_api/client_api/client_api_safe.h"

#include "aether/client_messages/client_messages_tele.h"

namespace ae {
P2pMessageStreamManager::P2pMessageStreamManager(AeContext const& ae_context,
                                                 Ptr<Client> const& client)
    : ae_context_{ae_context},
      client_{client},
      cloud_connection_{&client->cloud_connection()},
      on_message_received_sub_{CloudEventListener{
          ApiEventSubscriber{[this](ClientApiSafe& client_api, auto*) {
            return client_api.send_message_event().Subscribe(
                MethodPtr<&P2pMessageStreamManager::NewMessageReceived>{this});
          }},
          *cloud_connection_,
          RequestPolicy::Replica{cloud_connection_->count_connections()}}} {}

std::shared_ptr<ByteIStream> P2pMessageStreamManager::CreateStream(
    Uid destination) {
  CleanUpStreams();
  auto it = streams_.find(destination);
  if (it != std::end(streams_)) {
    // after cleanup only used streams should stay
    assert(!it->second.expired());
    return it->second.lock();
  }
  // create new stream
  auto stream = MakeStream(destination);
  auto byte_stream = std::shared_ptr<ByteIStream>{stream};
  streams_.emplace(destination, byte_stream);
  message_stream_update_subs_ += stream->stream_update_event().Subscribe(
      [this, dest{destination}]() { OnStreamUpdated(dest); });
  return byte_stream;
}

P2pMessageStreamManager::NewStreamEvent::Subscriber
P2pMessageStreamManager::new_stream_event() {
  return EventSubscriber{new_stream_event_};
}

void P2pMessageStreamManager::NewMessageReceived(AeMessage const& message) {
  AE_TELED_DEBUG("New message received {}", message.uid);
  auto it = streams_.find(message.uid);
  // if there is no stream or stream was closed
  if ((it == std::end(streams_)) || it->second.expired()) {
    auto p2p_stream = MakeStream(message.uid);
    auto byte_stream = std::shared_ptr<ByteIStream>{p2p_stream};
    streams_.emplace(message.uid, byte_stream);
    message_stream_update_subs_ += p2p_stream->stream_update_event().Subscribe(
        [this, dest{message.uid}]() { OnStreamUpdated(dest); });
    new_stream_event_.Emit(byte_stream);
    // write out first data
    p2p_stream->WriteOut(message.data);
  }
}

void P2pMessageStreamManager::CleanUpStreams() {
  // remove unused streams
  for (auto it = streams_.begin(); it != streams_.end();) {
    if (it->second.expired()) {
      it = streams_.erase(it);
    } else {
      ++it;
    }
  }
}

std::shared_ptr<P2pStream> P2pMessageStreamManager::MakeStream(
    Uid destination) {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  return std::make_shared<P2pStream>(ae_context_, client_ptr, destination);
}

void P2pMessageStreamManager::OnStreamUpdated(Uid destination) {
  auto it = streams_.find(destination);
  if (it == std::end(streams_)) {
    return;
  }
  auto stream = it->second.lock();
  if (!stream) {
    return;
  }
  auto info = stream->stream_info();
  // remove failed streams
  if (info.link_state == LinkState::kLinkError) {
    streams_.erase(it);
  }
}

}  // namespace ae
