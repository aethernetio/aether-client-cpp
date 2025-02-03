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

#ifndef AETHER_STREAM_API_SERIALIZE_STREAM_H_
#define AETHER_STREAM_API_SERIALIZE_STREAM_H_

#include <utility>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/api_protocol/api_message.h"

#include "aether/stream_api/istream.h"

namespace ae {
template <typename TIn, typename TOut>
class SerializeGate final : public Gate<TIn, TOut, DataBuffer, DataBuffer> {
 public:
  using Base = Gate<TIn, TOut, DataBuffer, DataBuffer>;

  ActionView<StreamWriteAction> Write(TIn&& in_data,
                                      TimePoint current_time) override {
    DataBuffer buffer;
    auto writer_buffer = VectorWriter<PackedSize>{buffer};
    auto output_stream = omstream{writer_buffer};
    output_stream << std::move(in_data);

    assert(Base::out_);
    return Base::out_->Write(std::move(buffer), current_time);
  }

  void LinkOut(typename Base::OutGate& out) override {
    Base::out_ = &out;

    Base::out_data_subscription_ = Base::out_->out_data_event().Subscribe(
        *this, MethodPtr<&SerializeGate::OnDataOutEvent>{});

    Base::gate_update_subscription_ = Base::out_->gate_update_event().Subscribe(
        Base::gate_update_event_, MethodPtr<&Base::GateUpdateEvent::Emit>{});
    Base::gate_update_event_.Emit();
  }

 private:
  void OnDataOutEvent(DataBuffer const& data) {
    auto reader_buffer = VectorReader<PackedSize>{data};
    auto input_stream = imstream{reader_buffer};

    TOut out_data{};
    input_stream >> out_data;
    Base::out_data_event_.Emit(std::move(out_data));
  }
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SERIALIZE_STREAM_H_
