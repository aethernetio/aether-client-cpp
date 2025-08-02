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

#include "aether/stream_api/safe_stream/receiving_chunk_list.h"

#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {
ReceiveChunkList::AddResult ReceiveChunkList::AddChunk(ReceivingChunk chunk) {
  /* Possible cases ARE:
   * - received a full duplicate of existing chunk
   * - chunk that is in range of existing chunk
   * - chunk that is partially overlaps existing chunk either left or right
   * - chunk that is overlaps a few chunks
   * - chunk with the most biggest offset
   * - chunk overlapped with already acknowledged
   */
  // find the biggest chunk
  auto pos =
      std::find_if(std::begin(chunks_), std::end(chunks_), [&](auto const& c) {
        return chunk.offset.IsBefore(c.offset) || (chunk.offset == c.offset);
      });

  // the same chunk
  if ((pos != std::end(chunks_)) &&
      (pos->offset_range() == chunk.offset_range())) {
    if (pos->repeat_count >= chunk.repeat_count) {
      return AddResult::kDuplicate;
    }
    pos->repeat_count = std::max(pos->repeat_count, chunk.repeat_count);
    return AddResult::kAdded;
  }

  if (pos != std::end(chunks_)) {
    // fix found chunk if overlaps
    auto chunk_range = chunk.offset_range();
    if (pos->offset_range().InRange(chunk_range.right)) {
      MergeChunkProperties(chunk, *pos);
      FixChunkOverlapsBegin(*pos, chunk_range.right + 1);
    }
  }

  // chunk is not the same with any of the existing chunks
  auto inserted_it = chunks_.insert(pos, std::move(chunk));
  auto next_offset = inserted_it->offset;
  // fix overlapping chunks offsets and remove empty
  chunks_.erase(std::remove_if(std::begin(chunks_), inserted_it,
                               [&](auto& c) {
                                 // overlapping offset
                                 auto range = c.offset_range();
                                 if (range.InRange(next_offset)) {
                                   MergeChunkProperties(*inserted_it, c);
                                   FixChunkOverlapsEnd(c, next_offset - 1);
                                 }
                                 // remove if empty
                                 return c.begin == c.end;
                               }),
                inserted_it);
  return AddResult::kAdded;
}

std::optional<ReceivingChunk> ReceiveChunkList::ReceiveChunk(
    SSRingIndex start_chunks) {
  auto chank_chain = GetContinuousChunkChain(start_chunks);
  if (chank_chain.empty()) {
    return std::nullopt;
  }

  auto data = JoinChunks(chank_chain);
  AE_TELED_DEBUG("Data chunk chain received length: {}", data.size());
  return ReceivingChunk{start_chunks, std::move(data), {}};
}

void ReceiveChunkList::Acknowledge(SSRingIndex from, SSRingIndex to) {
  chunks_.erase(std::remove_if(std::begin(chunks_), std::end(chunks_),
                               [&](auto& c) {
                                 // remove old chunks
                                 auto range = c.offset_range();
                                 if (range.IsBefore(from)) {
                                   return true;
                                 }
                                 if (range.IsBefore(to + 1)) {
                                   c.data.clear();
                                 }
                                 return false;
                               }),
                std::end(chunks_));
}

std::vector<MissedChunk> ReceiveChunkList::FindMissedChunks(
    SSRingIndex start_chunks) {
  auto it =
      std::find_if(std::begin(chunks_), std::end(chunks_), [&](auto const& c) {
        auto range = c.offset_range();
        return range.InRange(start_chunks) || range.IsAfter(start_chunks);
      });

  if (it == std::end(chunks_)) {
    return {};
  }

  std::vector<MissedChunk> res;
  auto next_chunk_offset = [&]() {
    if (auto range = it->offset_range(); range.InRange(start_chunks)) {
      return range.left;
    }
    return start_chunks;
  }();

  for (; it != std::end(chunks_); ++it) {
    // if got not expected chunk
    if (next_chunk_offset != it->offset) {
      res.emplace_back(MissedChunk{next_chunk_offset, &*it});
    }
    next_chunk_offset = it->offset_range().right + 1;
  }
  return res;
}

void ReceiveChunkList::Clear() { chunks_.clear(); }

DataBuffer ReceiveChunkList::JoinChunks(
    std::vector<std::pair<DataBuffer::iterator, DataBuffer::iterator>> const&
        chunk_chain) {
  DataBuffer data;
  // count size first
  std::size_t size = 0;
  for (auto const& c : chunk_chain) {
    size += static_cast<std::size_t>(std::distance(c.first, c.second));
  }
  data.reserve(size);
  // then copy data
  for (auto const& c : chunk_chain) {
    std::copy(c.first, c.second, std::back_inserter(data));
  }
  return data;
}

void ReceiveChunkList::FixChunkOverlapsBegin(ReceivingChunk& overlapped,
                                             SSRingIndex expected_offset) {
  // fix offset and begin if chunks overlap
  auto const distance = overlapped.offset.Distance(expected_offset);
  overlapped.offset = expected_offset;
  overlapped.begin = ((overlapped.end - overlapped.begin) > distance)
                         ? overlapped.begin + distance
                         : overlapped.end;
}

void ReceiveChunkList::FixChunkOverlapsEnd(ReceivingChunk& overlapped,
                                           SSRingIndex expected_end) {
  auto const distance = expected_end.Distance(overlapped.offset_range().right);
  overlapped.end = ((overlapped.end - overlapped.begin) > distance)
                       ? overlapped.end - distance
                       : overlapped.begin;
}

void ReceiveChunkList::MergeChunkProperties(ReceivingChunk& chunk,
                                            ReceivingChunk& overlapped) {
  chunk.repeat_count = std::max(overlapped.repeat_count, chunk.repeat_count);
}
std::vector<std::pair<DataBuffer::iterator, DataBuffer::iterator>>
ReceiveChunkList::GetContinuousChunkChain(SSRingIndex start_chunks) {
  auto from = std::find_if(
      std::begin(chunks_), std::end(chunks_),
      [&](auto const& c) { return c.offset_range().InRange(start_chunks); });
  if (from == std::end(chunks_)) {
    return {};
  }
  if (from->data.empty()) {
    return {};
  }

  std::vector<std::pair<DataBuffer::iterator, DataBuffer::iterator>> res;

  auto distance = from->offset.Distance(start_chunks);
  res.emplace_back(from->begin + distance, from->end);

  auto it = std::next(from);
  auto next_chunk_offset = from->offset_range().right + 1;
  for (; it != std::end(chunks_); it++) {
    auto& chunk = *it;
    if (next_chunk_offset != chunk.offset) {
      break;
    }
    if (chunk.data.empty()) {
      break;
    }
    next_chunk_offset = chunk.offset_range().right + 1;
    res.emplace_back(chunk.begin, chunk.end);
  }
  return res;
}

}  // namespace ae
