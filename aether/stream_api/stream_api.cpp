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

#include "aether/stream_api/stream_api.h"

#include <cstddef>

namespace ae {
StreamApiImpl::StreamApiImpl(ProtocolContext& protocol_context)
    : ApiClass{protocol_context}, stream{protocol_context} {}

void StreamApiImpl::Stream(ApiParser&, StreamId stream_id, DataBuffer data) {
  stream_event_.Emit(stream_id, data);
}

StreamApiImpl::StreamEvent::Subscriber StreamApiImpl::stream_event() {
  return EventSubscriber{stream_event_};
}

std::uint8_t StreamIdGenerator::GetNextClientStreamId() {
  static StreamId stream_id = 1;
  auto val = stream_id;
  stream_id += 2;
  return val;
}

std::uint8_t StreamIdGenerator::GetNextServerStreamId() {
  static StreamId stream_id = 2;
  auto val = stream_id;
  stream_id += 2;
  return val;
}

static constexpr std::size_t kStreamMessageOverhead =
    1 + 1 +
    sizeof(
        PackedSize::ValueType);  // message code + stream id +  child data size

StreamApiGate::StreamApiGate(StreamApiImpl& stream_api, StreamId stream_id)
    : stream_id_{stream_id},
      stream_api_{&stream_api},
      read_subscription_{stream_api_->stream_event().Subscribe(
          MethodPtr<&StreamApiGate::OnStream>{this})} {}

DataBuffer StreamApiGate::WriteIn(DataBuffer&& buffer) {
  auto api_call = ApiContext{*stream_api_};
  api_call->stream(stream_id_, std::move(buffer));
  return DataBuffer{std::move(api_call)};
}

void StreamApiGate::WriteOut(DataBuffer const& buffer) {
  out_data_event_.Emit(buffer);
}

EventSubscriber<void(DataBuffer const& data)> StreamApiGate::out_data_event() {
  return EventSubscriber{out_data_event_};
}

std::size_t StreamApiGate::Overhead() const { return kStreamMessageOverhead; }

void StreamApiGate::OnStream(StreamId stream_id, DataBuffer const& data) {
  if (stream_id_ == stream_id) {
    out_data_event_.Emit(data);
  }
}

}  // namespace ae
