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
  ReceiveChunkList() = default;

  std::optional<ReplacedChunk> AddChunk(ReceivingChunk&& chunk,
                                        SSRingIndex start_chunks,
                                        SSRingIndex ring_begin);

  std::optional<ReceivingChunk> PopChunks(SSRingIndex start_chunks);
  std::vector<MissedChunk> FindMissedChunks(SSRingIndex start_chunks);

  void Clear();

 private:
  void NormalizeChunks(SSRingIndex start_chunks, SSRingIndex ring_begin);
  std::pair<std::vector<ReceivingChunk>::iterator,
            std::vector<ReceivingChunk>::iterator>
  GetContinuousChunkChain(SSRingIndex start_chunks);
  DataBuffer JoinChunks(std::vector<ReceivingChunk>::iterator begin,
                        std::vector<ReceivingChunk>::iterator end);

  std::vector<ReceivingChunk> chunks_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_RECEIVING_CHUNK_LIST_H_
