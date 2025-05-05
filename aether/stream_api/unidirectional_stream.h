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

#ifndef AETHER_STREAM_API_UNIDIRECTIONAL_STREAM_H_
#define AETHER_STREAM_API_UNIDIRECTIONAL_STREAM_H_

#include "aether/stream_api/istream.h"

namespace ae {
template <typename TIn, typename TOut = TIn>
class ParallelStream final : public IStream<TIn, TOut> {
 public:
  using Base = IStream<TIn, TOut>;
  using OutDataEvent = typename Base::OutDataEvent;
  using StreamUpdateEvent = typename Base::StreamUpdateEvent;

  ParallelStream() = default;

  AE_CLASS_MOVE_ONLY(ParallelStream);

  ActionView<StreamWriteAction> Write(DataBuffer&& data) override {
    assert(write_stream_);
    return write_stream_->Write(std::move(data));
  }

  typename OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{out_data_event_};
  }

  typename StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{stream_update_event_};
  }

  StreamInfo stream_info() const override {
    if (write_stream_ == nullptr) {
      return StreamInfo{};
    }
    return write_stream_->stream_info();
  }

  void LinkWriteStream(Base& write_stream) {
    write_stream_ = &write_stream;
    write_gate_update_sub_ = write_stream_->stream_update_event().Subscribe(
        stream_update_event_, MethodPtr<&StreamUpdateEvent::Emit>{});
    stream_update_event_.Emit();
  }

  void LinkReadStream(Base& read_stream) {
    read_stream_ = &read_stream;
    out_data_sub_ = read_stream_->out_data_event().Subscribe(
        out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
    stream_update_event_.Emit();
  }

 private:
  Base* write_stream_;
  Base* read_stream_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;

  Subscription write_gate_update_sub_;
  Subscription out_data_sub_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_UNIDIRECTIONAL_STREAM_H_
