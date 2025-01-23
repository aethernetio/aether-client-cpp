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

#include "aether/stream_api/safe_stream/sending_chunk_list.h"

#include <iterator>
#include <algorithm>

namespace ae {

SendingChunkList::SendingChunkList(SafeStreamRingIndex::type window_size)
    : window_size_{window_size} {}

SendingChunk& SendingChunkList::Register(SafeStreamRingIndex begin,
                                         SafeStreamRingIndex end,
                                         TimePoint send_time) {
  auto offset_range = OffsetRange{begin, end, window_size_};

  auto it = std::find_if(
      std::begin(chunks_), std::end(chunks_),
      [&](auto const& sch) { return offset_range.InRange(sch.begin_offset); });

  if (it == std::end(chunks_)) {
    auto& ref = chunks_.emplace_back(
        SendingChunk{begin, end, std::uint16_t{}, send_time});
    return ref;
  }

  auto& sch = *it;
  if ((offset_range.begin == sch.begin_offset) &&
      (offset_range.end == sch.end_offset)) {
    // move it to the end
    sch.send_time = send_time;
    chunks_.splice(std::end(chunks_), chunks_, it);
    return chunks_.back();
  }

  auto& new_sch = chunks_.emplace_back(
      SendingChunk{begin, end, sch.repeat_count, send_time});

  // modify any overlapping chunks
  chunks_.remove_if([&](auto& chunk) {
    if (&chunk == &new_sch) {
      return false;
    }
    if (offset_range.InRange(chunk.begin_offset)) {
      new_sch.repeat_count = std::min(sch.repeat_count, chunk.repeat_count);

      if (offset_range.InRange(chunk.end_offset)) {
        // remove this fully overlapped chunk
        return true;
      }
      chunk.begin_offset = offset_range.end + 1;
    } else if (offset_range.InRange(chunk.end_offset)) {
      new_sch.repeat_count = std::min(sch.repeat_count, chunk.repeat_count);
      chunk.end_offset = offset_range.begin - 1;
    }
    return false;
  });

  return new_sch;
}

void SendingChunkList::RemoveUpTo(SafeStreamRingIndex offset) {
  chunks_.remove_if([&](auto& sch) {
    auto offset_range =
        OffsetRange{sch.begin_offset, sch.end_offset, window_size_};
    if (offset_range.Before(offset)) {
      return true;
    }
    if (offset_range.InRange(offset)) {
      sch.begin_offset = offset;
      // if chunk is collapsed
      return sch.begin_offset == sch.end_offset;
    }
    return false;
  });
}
}  // namespace ae
