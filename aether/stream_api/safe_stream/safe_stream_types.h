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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/ring_buffer.h"
#include "aether/transport/data_buffer.h"

namespace ae {

using SSRingIndex = RingIndex<std::uint16_t>;

struct OffsetRange {
  SSRingIndex left;
  SSRingIndex right;
  SSRingIndex ring_begin;

  // offset in [begin:end] range
  constexpr bool InRange(SSRingIndex offset) const {
    return (left(ring_begin) <= offset) && (right(ring_begin) >= offset);
  }

  // offset range is before offset ( end < offset)
  constexpr bool Before(SSRingIndex offset) const {
    return right(ring_begin) < offset;
  }

  // offset range is after offset ( begin > offset)
  constexpr bool After(SSRingIndex offset) const {
    return left(ring_begin) > offset;
  }

  constexpr auto distance() const { return left.Distance(right); }
};

struct SendingData {
  auto get_offset_range(SSRingIndex ring_begin) const {
    return OffsetRange{offset,
                       offset + static_cast<SSRingIndex::type>(data.size() - 1),
                       ring_begin};
  }

  SSRingIndex offset;
  DataBuffer data;
};

struct ReceivingChunk {
  ReceivingChunk(SSRingIndex off, DataBuffer&& d, std::uint16_t rc)
      : offset(off),
        data(std::move(d)),
        repeat_count(rc),
        begin{std::begin(data)},
        end{std::end(data)} {}

  auto get_offset_range(SSRingIndex ring_begin) const {
    auto distance = end - begin;
    return OffsetRange{offset,
                       offset + static_cast<SSRingIndex::type>(distance - 1),
                       ring_begin};
  }

  SSRingIndex offset;
  DataBuffer data;
  std::uint16_t repeat_count;
  DataBuffer::iterator begin;
  DataBuffer::iterator end;
};

struct SendingChunk {
  SSRingIndex begin_offset;
  SSRingIndex end_offset;
  std::uint16_t repeat_count;
  TimePoint send_time;
};

struct MissedChunk {
  SSRingIndex expected_offset;
  ReceivingChunk* chunk;
};

struct ReplacedChunk {
  SSRingIndex offset;
  std::uint16_t repeat_count;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_
