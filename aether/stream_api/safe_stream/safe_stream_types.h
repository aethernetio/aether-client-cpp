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

namespace ae {

using SafeStreamRingIndex = RingIndex<std::uint16_t>;

struct OffsetRange {
  SafeStreamRingIndex begin;
  SafeStreamRingIndex end;
  SafeStreamRingIndex::type window_size;

  // offset in [begin:end] range
  constexpr bool InRange(SafeStreamRingIndex offset) const {
    return (begin.Distance(offset) <= window_size) &&
           (offset.Distance(end) <= window_size);
  }

  // offset range is before offset ( end < offset)
  constexpr bool Before(SafeStreamRingIndex offset) const {
    return offset.Distance(end) > window_size;
  }

  // offset range is after offset ( begin > offset)
  constexpr bool After(SafeStreamRingIndex offset) const {
    return begin.Distance(offset) > window_size;
  }
};

struct OffsetTime {
  TimePoint time;
  SafeStreamRingIndex offset;
};

struct SendingChunk {
  SafeStreamRingIndex begin_offset;
  SafeStreamRingIndex end_offset;
  std::uint16_t repeat_count;
  TimePoint send_time;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_TYPES_H_
