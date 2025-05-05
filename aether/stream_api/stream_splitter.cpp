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

#include "aether/stream_api/stream_splitter.h"

namespace ae {
StreamSplitter::StreamSplitter() {
  stream_message_event_ =
      protocol_context_.MessageEvent<StreamApi::Stream>().Subscribe(
          *this, MethodPtr<&StreamSplitter::OnStream>{});
}

void StreamSplitter::LinkOut(OutStream& out) {
  out_ = &out;

  update_sub_ = out_->stream_update_event().Subscribe(
      stream_update_event_, MethodPtr<&StreamUpdateEvent::Emit>{});

  out_data_sub_ = out_->out_data_event().Subscribe(
      *this, MethodPtr<&StreamSplitter::OnDataEvent>{});
}

GatesStream<StreamApiGate>& StreamSplitter::RegisterStream(StreamId stream_id) {
  auto [new_stream, ok] = streams_.emplace(
      stream_id,
      make_unique<GatesStream>(StreamApiGate{protocol_context_, stream_id}));
  if (ok) {
    new_stream->second->LinkOut(*this);
  }
  return *new_stream->second;
}

StreamSplitter::NewStreamEvent::Subscriber StreamSplitter::new_stream_event() {
  return new_stream_event_;
}

void StreamSplitter::CloseStream(StreamId stream_id) {
  streams_.erase(stream_id);
}

std::size_t StreamSplitter::stream_count() const { return streams_.size(); }

void StreamSplitter::OnDataEvent(DataBuffer const& data) {
  auto api_parser = ApiParser(protocol_context_, data);
  auto api = StreamApi{};
  api_parser.Parse(api);
}

void StreamSplitter::OnStream(
    MessageEventData<StreamApi::Stream> const& message) {
  auto it = streams_.find(message.message().stream_id);
  if (it != streams_.end()) {
    return;
  }

  auto& new_stream = RegisterStream(message.message().stream_id);
  new_stream_event_.Emit(message.message().stream_id, new_stream);

  new_stream.WriteOut(message.message().child_data.PackData());
}
}  // namespace ae
