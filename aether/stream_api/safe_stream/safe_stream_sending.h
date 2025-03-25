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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SENDING_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SENDING_H_

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"

#include "aether/transport/data_buffer.h"

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/sending_chunk_list.h"
#include "aether/stream_api/safe_stream/sending_data_action.h"

namespace ae {

class SafeStreamSendingAction : public Action<SafeStreamSendingAction> {
 public:
  using SendEvent = Event<void(SafeStreamRingIndex offset, DataBuffer&& data,
                               TimePoint current_time)>;
  using RepeatEvent =
      Event<void(SafeStreamRingIndex offset, std::uint16_t repeat_count,
                 DataBuffer&& data, TimePoint current_time)>;

  SafeStreamSendingAction(ActionContext action_context,
                          SafeStreamConfig const& config);

  ~SafeStreamSendingAction() override;

  TimePoint Update(TimePoint current_time) override;

  SendEvent::Subscriber send_event();
  RepeatEvent::Subscriber repeat_event();

  /**
   * \brief Put new data to send
   */
  ActionView<SendingDataAction> SendData(DataBuffer data);

  void Confirm(SafeStreamRingIndex offset);
  void RequestRepeatSend(SafeStreamRingIndex offset);

  void ReportWriteSuccess(SafeStreamRingIndex offset);
  void ReportWriteStopped(SafeStreamRingIndex offset);
  void ReportWriteError(SafeStreamRingIndex offset);

  void set_max_data_size(std::size_t max_data_size);

 private:
  TimePoint HandleTimeouts(TimePoint current_time);
  void SendData(TimePoint current_time);
  void SendFirst(DataChunk&& chunk, TimePoint current_time);
  void SendRepeat(DataChunk&& chunk, std::uint16_t repeat_count,
                  TimePoint current_time);

  void ConfirmDataChunks(SafeStreamRingIndex offset);

  void StopSending(SafeStreamRingIndex offset);

  SafeStreamRingIndex::type buffer_capacity_;
  SafeStreamRingIndex::type window_size_;
  std::uint16_t max_repeat_count_;
  Duration wait_confirm_timeout_;
  SafeStreamApi safe_stream_api_;
  SafeStreamRingIndex::type max_data_size_;

  SendDataBuffer send_data_buffer_;
  SendingChunkList sending_chunks_;
  SendEvent send_event_;
  RepeatEvent repeat_event_;

  SafeStreamRingIndex last_confirmed_;
  SafeStreamRingIndex next_to_add_;
  SafeStreamRingIndex last_sent_offset_;

  MultiSubscription send_data_subscriptions_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_SENDING_H_
