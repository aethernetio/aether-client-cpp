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

SendingChunk& SendingChunkList::Register(SSRingIndex begin, SSRingIndex end,
                                         TimePoint send_time) {
  auto offset_range = OffsetRange{begin, end};

  // find the chunk with the same or overlapped offset
  // and if there is no one emplace it at the end of list
  auto it = std::find_if(std::begin(chunks_), std::end(chunks_),
                         [&](auto const& sch) {
                           return offset_range.InRange(sch.offset_range.left);
                         });

  if (it == std::end(chunks_)) {
    auto& ref = chunks_.emplace_back(SendingChunk{{begin, end}, {}, send_time});
    return ref;
  }

  // there is a chunk with the same or overlapped offset
  // if it's the same chunk, move it to the end
  if ((offset_range == it->offset_range)) {
    // move it to the end
    auto sch = *it;
    sch.send_time = send_time;
    chunks_.erase(it);
    chunks_.emplace_back(sch);
    return chunks_.back();
  }

  // !the worst case
  // chunk's offsets is overlapped
  // create a new chunk at the end of list and merge to it all the chunks with
  // overlapping offsets
  auto new_sch = SendingChunk{{begin, end}, it->repeat_count, send_time};

  // modify any overlapping chunks
  chunks_.erase(
      std::remove_if(
          std::begin(chunks_), std::end(chunks_),
          [&](auto& chunk) {
            if (offset_range.InRange(chunk.offset_range.left)) {
              new_sch.repeat_count =
                  std::min(new_sch.repeat_count, chunk.repeat_count);

              if (offset_range.InRange(chunk.offset_range.right)) {
                // remove this fully overlapped chunk
                return true;
              }
              chunk.offset_range.left = offset_range.right + 1;
            } else if (offset_range.InRange(chunk.offset_range.right)) {
              new_sch.repeat_count =
                  std::min(new_sch.repeat_count, chunk.repeat_count);
              chunk.offset_range.right = offset_range.left - 1;
            }
            return false;
          }),
      std::end(chunks_));

  return chunks_.emplace_back(new_sch);
}

void SendingChunkList::RemoveUpTo(SSRingIndex offset) {
  chunks_.erase(
      std::remove_if(
          std::begin(chunks_), std::end(chunks_),
          [&](auto& sch) {
            if (sch.offset_range.IsBefore(offset)) {
              return true;
            }
            if (sch.offset_range.InRange(offset)) {
              sch.offset_range.left = offset;
              // if chunk is collapsed
              return sch.offset_range.left.IsAfter(sch.offset_range.right) ||
                     (sch.offset_range.left == sch.offset_range.right);
            }
            return false;
          }),
      std::end(chunks_));
}
}  // namespace ae
