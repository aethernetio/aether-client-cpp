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

#include <vector>
#include <cstddef>

#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/sending_data_action.h"

namespace ae {
class SendDataBuffer {
 public:
  explicit SendDataBuffer(ActionContext action_context);

  /**
   * \brief Add new data to the sending buffer.
   * \param data - Sending data to send.
   * \return new view to SendingDataAction
   */
  ActionPtr<SendingDataAction> AddData(SendingData&& data);
  /**
   * \brief Get the slice of data to send.
   * \param offset - the begin offset from which to start the slice.
   * \param max_size - the maximum size of the slice.
   * \return DataChunk containing the slice of data and its offset.
   */
  DataChunk GetSlice(SSRingIndex offset, std::size_t max_size);

  /**
   * \brief Acknowledge data up to offset.
   * All the SendingDataAction fully acknowledged will through the result event.
   */
  std::size_t Acknowledge(SSRingIndex offset);
  /**
   * \brief Reject data from sending.
   * This removes all the sending data with less or overlapped offset range.
   * This leads to SendingDataAction through the error event.
   */
  std::size_t Reject(SSRingIndex offset);
  /**
   * \brief Stop data from sending.
   * This removes all the sending data with less or overlapped offset range.
   * This leads to SendingDataAction through the stop event.
   */
  std::size_t Stop(SSRingIndex offset);

  /**
   * \brief Get current buffer size in bytes.
   */
  std::size_t size() const { return buffer_size_; }

 private:
  ActionContext action_context_;

  // view store used for iteration over sending data
  std::vector<ActionPtr<SendingDataAction>> send_actions_;

  std::size_t buffer_size_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SEND_DATA_BUFFER_H_
