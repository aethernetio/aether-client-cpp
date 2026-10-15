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
#include <cstring>
#include <optional>
#include <vector>

#include "aether/types/data_buffer.h"

namespace ae::stream_matrix_bench {

enum class EnvelopeKind : std::uint8_t {
  kService = 0,
  kMeasure = 1,
};

struct BenchEnvelope {
  std::uint8_t kind{};
  std::uint64_t run_id{};
  std::uint64_t point_id{};
  std::uint64_t sequence{};
  std::vector<std::uint8_t> payload;
};

inline void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
  }
}

inline bool ReadU64(std::vector<std::uint8_t> const& in, std::size_t& pos,
                    std::uint64_t& v) {
  if (pos + 8 > in.size()) {
    return false;
  }
  v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(in[pos + i]) << (8 * i);
  }
  pos += 8;
  return true;
}

inline std::optional<BenchEnvelope> DeserializeEnvelope(DataBuffer const& data) {
  std::vector<std::uint8_t> in(data.begin(), data.end());
  if (in.size() < 1 + 8 * 3 + 4) {
    return std::nullopt;
  }
  std::size_t pos = 0;
  BenchEnvelope envelope;
  envelope.kind = in[pos++];
  if (!ReadU64(in, pos, envelope.run_id) ||
      !ReadU64(in, pos, envelope.point_id) ||
      !ReadU64(in, pos, envelope.sequence)) {
    return std::nullopt;
  }
  if (pos + 4 > in.size()) {
    return std::nullopt;
  }
  std::uint32_t len = 0;
  std::memcpy(&len, in.data() + pos, sizeof(len));
  pos += 4;
  if (pos + len > in.size()) {
    return std::nullopt;
  }
  envelope.payload.assign(in.begin() + pos, in.begin() + pos + len);
  return envelope;
}

inline DataBuffer SerializeEnvelope(BenchEnvelope const& envelope) {
  std::vector<std::uint8_t> out;
  out.reserve(1 + 8 * 3 + 4 + envelope.payload.size());
  out.push_back(envelope.kind);
  AppendU64(out, envelope.run_id);
  AppendU64(out, envelope.point_id);
  AppendU64(out, envelope.sequence);
  auto len = static_cast<std::uint32_t>(envelope.payload.size());
  auto const* len_bytes = reinterpret_cast<std::uint8_t const*>(&len);
  out.insert(out.end(), len_bytes, len_bytes + sizeof(len));
  out.insert(out.end(), envelope.payload.begin(), envelope.payload.end());
  return DataBuffer{std::move(out)};
}

inline std::size_t MeasureEnvelopeOverheadBytes() {
  return SerializeEnvelope(BenchEnvelope{}).size();
}

inline std::vector<std::uint8_t> MakePayloadPattern(std::size_t payload_bytes) {
  std::vector<std::uint8_t> bytes(payload_bytes);
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    bytes[i] = static_cast<std::uint8_t>(i & 0xFFU);
  }
  return bytes;
}

inline bool IsServiceEnvelope(BenchEnvelope const& e) {
  return e.kind == static_cast<std::uint8_t>(EnvelopeKind::kService);
}

inline bool IsMeasureEnvelope(BenchEnvelope const& e) {
  return e.kind == static_cast<std::uint8_t>(EnvelopeKind::kMeasure);
}

}  // namespace ae::stream_matrix_bench

#endif
