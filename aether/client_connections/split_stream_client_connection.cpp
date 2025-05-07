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

#include "aether/client_connections/split_stream_client_connection.h"

#include "aether/client_connections/client_connections_tele.h"

namespace ae {

SplitStreamCloudConnection::ConnectStream::ConnectStream(
    SplitStreamCloudConnection& self, Uid uid, StreamId id)
    : self_{&self}, uid_{uid}, id_{id} {}

SplitStreamCloudConnection::ConnectStream::~ConnectStream() {
  self_->CloseStream(uid_, id_);
}

SplitStreamCloudConnection::SplitStreamCloudConnection(
    ActionContext action_context, Ptr<Client> const& client,
    ClientConnection& client_connection)
    : client_connection_{&client_connection},
      action_context_{action_context},
      client_{client},
      client_new_stream_sub_{client_connection_->new_stream_event().Subscribe(
          *this, MethodPtr<&SplitStreamCloudConnection::OnNewStream>{})} {}

std::unique_ptr<ByteIStream> SplitStreamCloudConnection::CreateStream(
    Uid uid, StreamId id) {
  auto stream_it = streams_.find(uid);
  if (stream_it == std::end(streams_)) {
    stream_it = RegisterStream(uid);
  }

  auto& stream = stream_it->second->stream_splitter->RegisterStream(id);
  auto new_stream = make_unique<ConnectStream>(*this, uid, id);
  Tie(*new_stream, stream);
  return new_stream;
}

void SplitStreamCloudConnection::CloseStream(Uid uid, StreamId id) {
  auto stream_it = streams_.find(uid);
  if (stream_it == std::end(streams_)) {
    return;
  }
  stream_it->second->stream_splitter->CloseStream(id);
  if (stream_it->second->stream_splitter->stream_count() != 0) {
    return;
  }
  streams_.erase(stream_it);
}

SplitStreamCloudConnection::NewStreamEvent::Subscriber
SplitStreamCloudConnection::new_stream_event() {
  return EventSubscriber{new_stream_event_};
}

std::map<Uid,
         std::unique_ptr<SplitStreamCloudConnection::ClientStreams>>::iterator
SplitStreamCloudConnection::RegisterStream(Uid uid) {
  auto client_ptr = client_.Lock();
  assert(client_ptr);
  auto p2p_stream = make_unique<P2pStream>(action_context_, client_ptr, uid);
  auto stream_splitter = make_unique<StreamSplitter>();
  Tie(*stream_splitter, *p2p_stream);

  new_split_stream_subs_.Push(stream_splitter->new_stream_event().Subscribe(
      [this, uid](auto id, auto& stream) {
        auto new_stream = make_unique<ConnectStream>(*this, uid, id);
        Tie(*new_stream, stream);
        new_stream_event_.Emit(uid, id, std::move(new_stream));
      }));

  auto [stream_it, _] = streams_.emplace(
      uid, make_unique<ClientStreams>(ClientStreams{
               std::move(p2p_stream), std::move(stream_splitter)}));
  return stream_it;
}

void SplitStreamCloudConnection::OnNewStream(
    Uid uid, std::unique_ptr<ByteIStream> stream) {
  auto stream_it = streams_.find(uid);
  if (stream_it != std::end(streams_)) {
    // stream already exists
    AE_TELED_WARNING("Get event for new stream with same uid {}", uid);
    return;
  }

  auto client_ptr = client_.Lock();
  assert(client_ptr);

  auto p2p_stream = make_unique<P2pStream>(
      action_context_, std::move(client_ptr), uid, std::move(stream));
  auto stream_splitter = make_unique<StreamSplitter>();
  Tie(*stream_splitter, *p2p_stream);

  new_split_stream_subs_.Push(stream_splitter->new_stream_event().Subscribe(
      [this, uid](auto id, auto& stream) {
        auto new_stream = make_unique<ConnectStream>(*this, uid, id);
        Tie(*new_stream, stream);
        new_stream_event_.Emit(uid, id, std::move(new_stream));
      }));

  streams_.emplace(uid,
                   make_unique<ClientStreams>(ClientStreams{
                       std::move(p2p_stream), std::move(stream_splitter)}));
}

}  // namespace ae
