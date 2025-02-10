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

#include "aether/stream_api/splitter_gate.h"

namespace ae {
SplitterGate::SplitterGate() {
  stream_message_event_ =
      protocol_context_.MessageEvent<StreamApi::Stream>().Subscribe(
          *this, MethodPtr<&SplitterGate::OnStream>{});
}

void SplitterGate::LinkOut(OutGate& out) {
  out_ = &out;
  gate_update_subscription_ = out_->gate_update_event().Subscribe(
      gate_update_event_, MethodPtr<&GateUpdateEvent::Emit>{});

  out_data_subscription_ = out_->out_data_event().Subscribe(
      *this, MethodPtr<&SplitterGate::OnDataEvent>{});

  gate_update_event_.Emit();
}

StreamApiGate& SplitterGate::RegisterStream(StreamId stream_id) {
  auto [new_stream, ok] =
      streams_.try_emplace(stream_id, protocol_context_, stream_id);
  if (ok) {
    new_stream->second.LinkOut(*this);
  }
  return new_stream->second;
}

SplitterGate::NewStreamEvent::Subscriber SplitterGate::new_stream_event() {
  return new_stream_event_;
}

void SplitterGate::CloseStream(StreamId stream_id) {
  streams_.erase(stream_id);
}

std::size_t SplitterGate::stream_count() const { return streams_.size(); }

void SplitterGate::OnDataEvent(DataBuffer const& data) {
  auto api_parser = ApiParser(protocol_context_, data);
  auto api = StreamApi{};
  api_parser.Parse(api);
}

void SplitterGate::OnStream(
    MessageEventData<StreamApi::Stream> const& message) {
  auto it = streams_.find(message.message().stream_id);
  if (it != streams_.end()) {
    return;
  }

  auto& new_stream = RegisterStream(message.message().stream_id);
  new_stream_event_.Emit(message.message().stream_id, new_stream);

  new_stream.PutData(message.message().child_data.PackData());
}
}  // namespace ae
