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

#ifndef AETHER_STREAM_API_STREAM_SPLITTER_H_
#define AETHER_STREAM_API_STREAM_SPLITTER_H_

#include <map>
#include <cstddef>

#include "aether/events/events.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/gates_stream.h"

namespace ae {
class StreamSplitter : public ByteStream {
 public:
  using NewStreamEvent =
      Event<void(StreamId stream_id, GatesStream<StreamApiGate>& stream)>;

  StreamSplitter();

  AE_CLASS_NO_COPY_MOVE(StreamSplitter);

  void LinkOut(OutStream& out) override;

  GatesStream<StreamApiGate>& RegisterStream(StreamId stream_id);
  NewStreamEvent::Subscriber new_stream_event();
  void CloseStream(StreamId stream_id);
  std::size_t stream_count() const;

 private:
  void OnDataEvent(DataBuffer const& data_buffer);
  void OnStream(MessageEventData<StreamApi::Stream> const& message);

  ProtocolContext protocol_context_;
  Subscription stream_message_event_;
  NewStreamEvent new_stream_event_;

  std::map<StreamId, std::unique_ptr<GatesStream<StreamApiGate>>> streams_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_STREAM_SPLITTER_H_
