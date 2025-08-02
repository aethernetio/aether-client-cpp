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

#ifndef AETHER_STREAM_API_SAFE_STREAM_RECEIVING_CHUNK_LIST_H_
#define AETHER_STREAM_API_SAFE_STREAM_RECEIVING_CHUNK_LIST_H_

#include <vector>
#include <optional>

#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
class ReceiveChunkList {
 public:
  enum class AddResult : std::uint8_t {
    kDuplicate,
    kAdded,
  };

  ReceiveChunkList() = default;

  AddResult AddChunk(ReceivingChunk chunk);

  std::optional<ReceivingChunk> ReceiveChunk(SSRingIndex start_chunks);
  void Acknowledge(SSRingIndex from, SSRingIndex to);
  std::vector<MissedChunk> FindMissedChunks(SSRingIndex start_chunks);

  void Clear();

  std::size_t size() const { return chunks_.size(); }
  bool empty() const { return chunks_.empty(); }

 private:
  static DataBuffer JoinChunks(
      std::vector<std::pair<DataBuffer::iterator, DataBuffer::iterator>> const&
          chunk_chain);

  static void FixChunkOverlapsBegin(ReceivingChunk& overlapped,
                                    SSRingIndex expected_offset);
  static void FixChunkOverlapsEnd(ReceivingChunk& overlapped,
                                  SSRingIndex expected_end);
  static void MergeChunkProperties(ReceivingChunk& chunk,
                                   ReceivingChunk& overlapped);

  std::vector<std::pair<DataBuffer::iterator, DataBuffer::iterator>>
  GetContinuousChunkChain(SSRingIndex start_chunks);

  std::vector<ReceivingChunk> chunks_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_RECEIVING_CHUNK_LIST_H_
