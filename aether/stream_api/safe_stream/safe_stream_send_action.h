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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SEND_ACTION_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SEND_ACTION_H_

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/stream_write_action.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/sending_chunk_list.h"

namespace ae {
class ISendDataPush {
 public:
  virtual ~ISendDataPush() = default;
  virtual ActionView<StreamWriteAction> PushData(
      DataChunk &&data_chunk, std::uint16_t repeat_count) = 0;
};

class SafeStreamSendAction : public Action<SafeStreamSendAction> {
 public:
  SafeStreamSendAction(ActionContext action_context,
                       ISendDataPush &send_data_push);

  AE_CLASS_NO_COPY_MOVE(SafeStreamSendAction)

  ActionResult Update(TimePoint current_time);

  bool Confirm(SSRingIndex confirm_offset);
  void RequestRepeat(SSRingIndex request_offset);

  void SetOffset(SSRingIndex begin_offset);
  void SetConfig(std::uint16_t max_repeat_count_, std::size_t max_packet_size,
                 SSRingIndex::type window_size, Duration wait_confirm_timeout);
  ActionView<SendingDataAction> SendData(DataBuffer &&data);

 private:
  void SendChunk();
  void Send(std::uint16_t repeat_count, DataChunk &&data_chunk,
            TimePoint current_time);
  void RejectSend(SendingChunk &sending_chunk);
  TimePoint SendTimeouts(TimePoint current_time);

  ActionView<StreamWriteAction> PushData(DataChunk &&data_chunk,
                                         std::uint16_t repeat_count);

  ISendDataPush *send_data_push_;

  SSRingIndex begin_;       //< begin data offset
  SSRingIndex last_sent_;   //< last offset for last sent data
  SSRingIndex last_added_;  //< last offset for last sent data

  std::uint16_t max_repeat_count_;
  SSRingIndex::type max_packet_size_;
  SSRingIndex::type window_size_;
  Duration wait_confirm_timeout_;

  SendDataBuffer send_data_buffer_;
  SendingChunkList sending_chunks_;

  MultiSubscription sending_data_subs_;
  MultiSubscription send_subs_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SEND_ACTION_H_
