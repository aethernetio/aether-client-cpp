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

#ifndef AETHER_STREAM_API_SERIALIZE_GATE_H_
#define AETHER_STREAM_API_SERIALIZE_GATE_H_

#include <utility>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/api_protocol/api_message.h"

#include "aether/stream_api/istream.h"

namespace ae {
template <typename TIn, typename TOut>
class SerializeGate {
 public:
  DataBuffer WriteIn(TIn&& in_data) {
    DataBuffer buffer;
    auto writer_buffer = VectorWriter<PackedSize>{buffer};
    auto output_stream = omstream{writer_buffer};
    output_stream << std::move(in_data);
    return buffer;
  }

  TOut WriteOut(DataBuffer const& data) {
    auto reader_buffer = VectorReader<PackedSize>{data};
    auto input_stream = imstream{reader_buffer};

    TOut out_data{};
    input_stream >> out_data;
    return out_data;
  }
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SERIALIZE_GATE_H_
