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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SEND_DATA_BUFFER_H_
#define AETHER_STREAM_API_SAFE_STREAM_SEND_DATA_BUFFER_H_

#include <list>
#include <cstddef>

#include "aether/actions/action_list.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/sending_data_action.h"

namespace ae {
struct DataChunk {
  DataBuffer data;
  SSRingIndex offset;
};

class SendDataBuffer {
 public:
  explicit SendDataBuffer(ActionContext action_context);

  // add more data to the buffer
  ActionView<SendingDataAction> AddData(SendingData&& data);
  // get data slice from the buffer
  DataChunk GetSlice(SSRingIndex offset, std::size_t max_size,
                     SSRingIndex ring_begin);

  void MoveOffset(SSRingIndex::type distance);

  // confirm data has been sent
  std::size_t Confirm(SSRingIndex offset, SSRingIndex ring_begin);
  // sending reject
  std::size_t Reject(SSRingIndex offset, SSRingIndex ring_begin);
  // sending stop, remove size of removed
  std::size_t Stop(SSRingIndex offset, SSRingIndex ring_begin);
  std::size_t size() const { return buffer_size_; }

 private:
  ActionList<SendingDataAction> send_actions_;
  std::list<ActionView<SendingDataAction>> send_action_views_;

  std::size_t buffer_size_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SEND_DATA_BUFFER_H_
