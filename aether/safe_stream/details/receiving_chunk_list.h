/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_SAFE_STREAM_DETAILS_RECEIVING_CHUNK_LIST_H_
#define AETHER_SAFE_STREAM_DETAILS_RECEIVING_CHUNK_LIST_H_

#include <vector>
#include <optional>
#include <algorithm>

#include "aether/types/ring_index.h"

namespace ae {
enum class ChunkAddResult : std::uint8_t {
  kInvalid,
  kDuplicate,
  kAdded,
  kAddRepeated,
};

template <typename Index>
class ReceiveChunkList {
 public:
  using IndexType = Index;
  using IndexRangeType = RingIndexRange<Index>;

  struct Chunk {
    IndexRangeType range;
    std::uint8_t repeat_count;
  };

  struct Comparator {
    using is_transparent = std::true_type;

    bool operator()(IndexType a, IndexType b) const {
      return IndexComparable{a, self->buffer_begin_} < b;
    }

    ReceiveChunkList* self;
  };

  explicit ReceiveChunkList(IndexType buffer_begin) noexcept
      : buffer_begin_{buffer_begin}, chunks_{Comparator{.self = this}} {}

  void set_buffer_begin(IndexType buffer_begin) {
    buffer_begin_ = buffer_begin;
  }

  ChunkAddResult AddChunk(IndexRangeType range, std::uint8_t repeat_count) {
    auto [it, inserted] =
        chunks_.emplace(range.left, Chunk{range, repeat_count});
    if (!inserted && (it->second.range == range)) {
      if (it->second.repeat_count >= repeat_count) {
        return ChunkAddResult::kDuplicate;
      }
      it->second.repeat_count = repeat_count;
      return ChunkAddResult::kAddRepeated;
    }
    // add range in sorted order
    auto pos = std::lower_bound(std::begin(ranges_), std::end(ranges_), range,
                                [&](auto const& a, auto const& b) {
                                  return IndexComparable{
                                             a.left, buffer_begin_} < b.left;
                                });
    ranges_.insert(pos, range);
    return ChunkAddResult::kAdded;
  }

  IndexRangeType ReceiveChunk() const {
    IndexRangeType range;
    range.left = buffer_begin_;
    range.right = buffer_begin_;
    auto expected_index = range.left;

    for (auto const& r : ranges_) {
      // if there is a gap, break
      if (IndexComparable{r.left, buffer_begin_} > expected_index) {
        break;
      }
      // if c.right is after range.right, update range.right
      if (IndexComparable{r.right, buffer_begin_} > range.right) {
        range.right = r.right;
      }
      expected_index = range.right + 1;
    }
    return range;
  }

  void Acknowledge(IndexType to) {
    // remove chunks that are before or equal to `to`
    std::erase_if(chunks_, [&](auto const& c) {
      return IndexComparable{c.first, buffer_begin_} <= to;
    });
    // remove ranges that are before or equal to `to`
    std::erase_if(ranges_, [&](auto& r) {
      if (IndexComparable{r.right, buffer_begin_} <= to) {
        return true;
      }
      // if range overlaps with `to`, shift left boundary
      if (IndexComparable{r.left, buffer_begin_} <= to) {
        r.left = to + 1;
        return false;
      }
      return false;
    });
  }

  std::optional<IndexRangeType> FindMissedChunk() const {
    auto expected = buffer_begin_;
    for (auto const& r : ranges_) {
      if (IndexComparable{r.left, buffer_begin_} > expected) {
        IndexRangeType missed{.left = expected, .right = r.left - 1};
        return missed;
      }
      expected = r.right + 1;
    }
    return std::nullopt;
  }

  bool empty() const { return chunks_.empty(); }

 private:
  IndexType buffer_begin_{};
  std::map<Index, Chunk, Comparator> chunks_;
  std::vector<IndexRangeType> ranges_;
};
}  // namespace ae

#endif  // AETHER_SAFE_STREAM_DETAILS_RECEIVING_CHUNK_LIST_H_
