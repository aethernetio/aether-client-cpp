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
std::optional<ReplacedChunk> ReceiveChunkList::AddChunk(
    ReceivingChunk&& chunk, SSRingIndex start_chunks, SSRingIndex ring_begin) {
  /* Possible cases ARE:
   * - received a full duplicate of existing chunk
   * - chunk that is in range of existing chunk
   * - chunk that is partially overlaps existing chunk either left or right
   * - chunk that is overlaps a few chunks
   * - chunk with the most biggest offset
   * - chunk overlapped with already confirmed
   */

  auto chunk_range = chunk.get_offset_range(ring_begin);
  if (chunk_range.Before(start_chunks)) {
    AE_TELED_WARNING("Chunk is duplicated with already confirmed");
    // the chunk is confirmed, do not add
    return std::nullopt;
  }

  auto it =
      std::find_if(std::begin(chunks_), std::end(chunks_), [&](auto const& ch) {
        return (chunk.offset(ring_begin) == ch.offset) &&
               (chunk.data.size() == ch.data.size());
      });

  if (it != std::end(chunks_)) {
    // full duplication
    AE_TELED_DEBUG("Chunk is duplicated with the received one");
    auto replace_chunk = ReplacedChunk{it->offset, it->repeat_count};
    it->repeat_count = std::max(chunk.repeat_count, it->repeat_count);
    return replace_chunk;
  }

  AE_TELED_DEBUG("Emplace back chunk");
  auto& inserted = chunks_.emplace_back(std::move(chunk));
  auto replace_chunk = ReplacedChunk{inserted.offset, inserted.repeat_count};

  NormalizeChunks(start_chunks, ring_begin);
  return replace_chunk;
}

std::optional<ReceivingChunk> ReceiveChunkList::PopChunks(
    SSRingIndex start_chunks) {
  auto [from, to] = GetContinuousChunkChain(start_chunks);
  if (std::distance(from, to) == 0) {
    return std::nullopt;
  }

  auto data = JoinChunks(from, to);
  AE_TELED_DEBUG("Data chunk chain received length: {}", data.size());
  chunks_.erase(from, to);
  return ReceivingChunk{start_chunks, std::move(data), {}};
}

std::vector<MissedChunk> ReceiveChunkList::FindMissedChunks(
    SSRingIndex start_chunks) {
  std::vector<MissedChunk> res;
  auto next_chunk_offset = start_chunks;
  for (auto& chunk : chunks_) {
    // if got not expected chunk
    if (next_chunk_offset(start_chunks) != chunk.offset) {
      res.emplace_back(MissedChunk{next_chunk_offset, &chunk});
    }
    next_chunk_offset =
        chunk.offset + chunk.get_offset_range(start_chunks).distance() + 1;
  }
  return res;
}

void ReceiveChunkList::Clear() { chunks_.clear(); }

void ReceiveChunkList::NormalizeChunks(SSRingIndex start_chunks,
                                       SSRingIndex ring_begin) {
  /*
   * Sort chunks and fix overlappings
   */
  std::sort(std::begin(chunks_), std::end(chunks_),
            [ring_begin](auto const& left, auto const& right) {
              return left.offset(ring_begin) < right.offset;
            });
  SSRingIndex next_chunk_offset = start_chunks - 1;
  chunks_.erase(std::remove_if(std::begin(chunks_), std::end(chunks_),
                               [&](auto& ch) {
                                 auto range = ch.get_offset_range(ring_begin);
                                 if (range.InRange(next_chunk_offset)) {
                                   // fix offset and begin if chunks overlap
                                   auto distance = ch.offset.Distance(
                                       next_chunk_offset + 1);
                                   ch.offset += distance;
                                   ch.begin = ((ch.end - ch.begin) > distance)
                                                  ? ch.begin + distance
                                                  : ch.end;
                                 }
                                 next_chunk_offset = range.right;
                                 return ch.begin == ch.end;
                               }),
                std::end(chunks_));
}

std::pair<std::vector<ReceivingChunk>::iterator,
          std::vector<ReceivingChunk>::iterator>
ReceiveChunkList::GetContinuousChunkChain(SSRingIndex start_chunks) {
  auto next_chunk_offset = start_chunks;
  auto from = std::begin(chunks_);
  auto it = from;

  for (; it != std::end(chunks_); it++) {
    auto& chunk = *it;
    if (next_chunk_offset(start_chunks) == chunk.offset) {
      next_chunk_offset =
          chunk.offset + chunk.get_offset_range(start_chunks).distance() + 1;
    } else {
      break;
    }
  }
  return std::make_pair(from, it);
}

DataBuffer ReceiveChunkList::JoinChunks(
    std::vector<ReceivingChunk>::iterator begin,
    std::vector<ReceivingChunk>::iterator end) {
  DataBuffer data;
  // count size first
  std::size_t size = 0;
  for (auto it = begin; it != end; it++) {
    size += static_cast<std::size_t>(std::distance(it->begin, it->end));
  }
  data.reserve(size);
  // then copy data
  for (auto it = begin; it != end; it++) {
    std::copy(it->begin, it->end, std::back_inserter(data));
  }
  return data;
}

}  // namespace ae
