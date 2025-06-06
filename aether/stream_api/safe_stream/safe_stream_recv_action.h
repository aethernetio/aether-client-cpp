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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_RECV_ACTION_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_RECV_ACTION_H_

#include <optional>

#include "aether/common.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/receiving_chunk_list.h"

namespace ae {
class ISendConfirmRepeat {
 public:
  virtual ~ISendConfirmRepeat() = default;

  virtual void SendConfirm(SSRingIndex offset) = 0;
  virtual void SendRepeatRequest(SSRingIndex offset) = 0;
};

class SafeStreamRecvAction : public Action<SafeStreamRecvAction> {
 public:
  using ReceiveEvent = Event<void(DataBuffer&& data)>;

  SafeStreamRecvAction(ActionContext action_context,
                       ISendConfirmRepeat& send_confirm_repeat);

  AE_CLASS_NO_COPY_MOVE(SafeStreamRecvAction)

  TimePoint Update(TimePoint current_time) override;

  void PushData(DataBuffer&& data, SSRingIndex received_offset,
                std::uint16_t repeat_count);

  void SetOffset(SSRingIndex offset);
  void SetConfig(SSRingIndex::type window_size, Duration send_confirm_timeout,
                 Duration send_repeat_timeout);

  ReceiveEvent::Subscriber receive_event();

 private:
  void ChecklCompletedChains();
  TimePoint CheckConfirmations(TimePoint current_time);
  TimePoint CheckMissing(TimePoint current_time);
  void SendConfirmation(SSRingIndex offset);
  void SendRequestRepeat(SSRingIndex offset);

  ISendConfirmRepeat* send_confirm_repeat_;

  SSRingIndex begin_;         //< last data offset confirmed
  SSRingIndex last_emitted_;  //< last data offset emitted
  ReceiveChunkList chunks_;

  SSRingIndex::type window_size_;
  Duration send_confirm_timeout_;
  Duration send_repeat_timeout_;

  std::optional<TimePoint> sent_confirm_;
  std::optional<TimePoint> repeat_request_;

  ReceiveEvent receive_event_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_RECV_ACTION_H_
