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
#include "aether/config.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/types/statistic_counter.h"
#include "aether/events/multi_subscription.h"
#include "aether/write_action/write_action.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/sending_chunk_list.h"

namespace ae {
class ISendDataPush {
 public:
  virtual ~ISendDataPush() = default;
  virtual ActionPtr<WriteAction> PushData(SSRingIndex begin,
                                          DataMessage&& data_message) = 0;
};

class SafeStreamSendAction : public Action<SafeStreamSendAction> {
 public:
  using ResponseStatistics =
      StatisticsCounter<Duration, AE_STATISTICS_SAFE_STREAM_WINDOW_SIZE>;

  SafeStreamSendAction(ActionContext action_context,
                       ISendDataPush& send_data_push,
                       SafeStreamConfig const& config);

  AE_CLASS_NO_COPY_MOVE(SafeStreamSendAction)

  UpdateStatus Update(TimePoint current_time);

  bool Acknowledge(SSRingIndex confirm_offset);
  void RequestRepeat(SSRingIndex request_offset);

  void SetMaxPayload(std::size_t max_payload_size);

  ActionPtr<SendingDataAction> SendData(DataBuffer&& data);

 private:
  void SendChunk(TimePoint current_time);
  void Send(std::uint16_t repeat_count, DataChunk&& data_chunk,
            TimePoint current_time);
  void RejectSend(SendingChunk& sending_chunk);
  UpdateStatus SendTimeouts(TimePoint current_time);

  ActionPtr<WriteAction> PushData(DataBuffer&& data_buffer,
                                  SSRingIndex::type delta,
                                  std::uint8_t repeat_count);

  ISendDataPush* send_data_push_;

  SSRingIndex begin_;       //< begin data offset
  SSRingIndex last_sent_;   //< last offset for last sent data
  SSRingIndex last_added_;  //< last offset for last sent data
  bool init_state_;

  std::uint8_t max_repeat_count_;
  SSRingIndex::type max_payload_size_;
  SSRingIndex::type window_size_;

  SendDataBuffer send_data_buffer_;
  SendingChunkList sending_chunks_;

  MultiSubscription sending_data_subs_;
  MultiSubscription send_subs_;
  ResponseStatistics response_statistics_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SEND_ACTION_H_
