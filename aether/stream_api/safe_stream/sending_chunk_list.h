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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SENDING_CHUNK_LIST_H_
#define AETHER_STREAM_API_SAFE_STREAM_SENDING_CHUNK_LIST_H_

#include <list>

#include "aether/common.h"

#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
class SendingChunkList {
 public:
  explicit SendingChunkList(SafeStreamRingIndex::type window_size);

  /**
   * \brief Register a new sending chunk.
   * If chunk with that offset does not exist, it will be created at the end of
   * the list. Otherwise, it will be moved to the end of the list and possibly
   * merged with other chunks if offsets are overlaps.
   */
  SendingChunk& Register(SafeStreamRingIndex begin, SafeStreamRingIndex end,
                         TimePoint send_time);

  /**
   * \brief Remove all chunks up to the given offset.
   */
  void RemoveUpTo(SafeStreamRingIndex offset);

  SendingChunk& front() { return chunks_.front(); }
  bool empty() const { return chunks_.empty(); }
  std::size_t size() const { return chunks_.size(); }

 private:
  SafeStreamRingIndex::type window_size_;
  std::list<SendingChunk> chunks_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SENDING_CHUNK_LIST_H_
