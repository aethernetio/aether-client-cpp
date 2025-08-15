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
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/receiving_chunk_list.h"

namespace ae {
class ISendAckRepeat {
 public:
  virtual ~ISendAckRepeat() = default;

  virtual void SendAck(SSRingIndex offset) = 0;
  virtual void SendRepeatRequest(SSRingIndex offset) = 0;
};

class SafeStreamRecvAction : public Action<SafeStreamRecvAction> {
 public:
  using ReceiveEvent = Event<void(DataBuffer&& data)>;

  SafeStreamRecvAction(ActionContext action_context,
                       ISendAckRepeat& send_confirm_repeat,
                       SafeStreamConfig const& config);

  AE_CLASS_NO_COPY_MOVE(SafeStreamRecvAction)

  UpdateStatus Update(TimePoint current_time);

  void PushData(SSRingIndex begin, DataMessage data_message);

  ReceiveEvent::Subscriber receive_event();

 private:
  void HandleData(SSRingIndex received_offset, std::uint8_t repeat_count,
                  DataBuffer&& data);

  void CheckCompletedChains();
  UpdateStatus CheckAcknowledgement(TimePoint current_time);
  UpdateStatus CheckMissing(TimePoint current_time);
  void SendAcknowledgement(SSRingIndex offset);
  void SendRequestRepeat(SSRingIndex offset);

  ISendAckRepeat* send_confirm_repeat_;

  std::optional<SSRingIndex> session_start_;  //< current session start offset
  std::optional<SSRingIndex> begin_;          //< last data offset confirmed
  SSRingIndex last_emitted_;                  //< last data offset emitted
  ReceiveChunkList chunks_;

  Duration send_ack_timeout_;
  Duration send_repeat_timeout_;
  SSRingIndex::type window_size_;

  std::optional<TimePoint> sent_ack_time_;
  std::optional<TimePoint> repeat_request_time_;
  bool acknowledgement_req_;

  ReceiveEvent receive_event_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_RECV_ACTION_H_
