/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_ENVELOPE_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_ENVELOPE_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/types/data_buffer.h"

namespace ae::stream_matrix_bench {

struct BenchEnvelope {
  std::uint64_t sequence{};
  std::vector<std::uint8_t> payload;
  AE_REFLECT_MEMBERS(sequence, payload)
};

inline std::optional<BenchEnvelope> DeserializeEnvelope(DataBuffer const& data) {
  std::vector<std::uint8_t> bytes(data.begin(), data.end());
  seri::BinaryVectorBuffer<> buffer{bytes};
  seri::BinaryArchive archive{buffer};
  BenchEnvelope envelope;
  if (!archive.Load(seri::Meta{envelope})) {
    return std::nullopt;
  }
  return envelope;
}

inline DataBuffer SerializeEnvelope(BenchEnvelope const& envelope) {
  std::vector<std::uint8_t> bytes;
  seri::BinaryArchive archive{seri::BinaryVectorBuffer<>{bytes}};
  archive.Save(seri::Meta{envelope});
  return DataBuffer{std::move(bytes)};
}

inline std::size_t MeasureEnvelopeOverheadBytes() {
  BenchEnvelope probe{.sequence = 0, .payload = {}};
  return SerializeEnvelope(probe).size();
}

inline std::vector<std::uint8_t> MakePayloadPattern(std::size_t payload_bytes) {
  std::vector<std::uint8_t> bytes(payload_bytes);
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    bytes[i] = static_cast<std::uint8_t>(i & 0xFFU);
  }
  return bytes;
}

}  // namespace ae::stream_matrix_bench

#endif  // EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_ENVELOPE_H_
