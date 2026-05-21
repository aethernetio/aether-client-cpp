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

#ifndef AETHER_SAFE_STREAM_DETAILS_SENDING_CHUNK_LIST_H_
#define AETHER_SAFE_STREAM_DETAILS_SENDING_CHUNK_LIST_H_

#include <vector>

#include "aether/clock.h"
#include "aether/types/ring_index.h"

namespace ae {
template <typename Index>
class SendingChunkList {
 public:
  using IndexRangeType = RingIndexRange<Index>;

  struct Chunk {
    IndexRangeType range;
    TimePoint send_time;
    std::uint8_t repeat_count;
  };

  explicit SendingChunkList(Index buffer_begin) : buffer_begin_{buffer_begin} {}

  void set_buffer_begin(Index buffer_begin) { buffer_begin_ = buffer_begin; }

  /**
   * \brief Register a new sending chunk.
   * If chunk with that index does not exist, it will be created at the end
   * of the list. Otherwise, it will be moved to the end of the list and
   * updated.
   */
  Chunk& Register(IndexRangeType chunk_range, TimePoint send_time) {
    auto old_it = std::find_if(
        std::begin(chunks_), std::end(chunks_),
        [&](auto const& c) { return c.range.left == chunk_range.left; });

    auto chunk =
        Chunk{.range = chunk_range, .send_time = send_time, .repeat_count{}};

    if (old_it != std::end(chunks_)) {
      chunk.repeat_count = old_it->repeat_count;
      chunks_.erase(old_it);
    }
    return chunks_.emplace_back(chunk);
  }

  /**
   * \brief Remove all chunks up to the given offset.
   */
  void RemoveUpTo(Index to) {
    std::erase_if(chunks_, [&](auto const& c) {
      return IndexComparable{c.range.right, buffer_begin_} <= to;
    });
  }

  Chunk* Select(IndexRangeType range) {
    // TODO: make more precise selection
    for (auto& c : chunks_) {
      if (range.InRange(c.range.left, buffer_begin_)) {
        return &c;
      }
    }
    return nullptr;
  }

  Chunk& front() { return chunks_.front(); }
  bool empty() const { return chunks_.empty(); }

 private:
  Index buffer_begin_;
  // TODO: make a logic without allocations and resorting
  std::vector<Chunk> chunks_;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_SENDING_CHUNK_LIST_H_
