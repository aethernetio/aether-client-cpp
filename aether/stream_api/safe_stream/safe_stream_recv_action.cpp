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

#include "aether/stream_api/safe_stream/safe_stream_recv_action.h"

#include <cassert>
#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {
SafeStreamRecvAction::SafeStreamRecvAction(ActionContext action_context,
                                           ISendAckRepeat& send_confirm_repeat,
                                           SafeStreamConfig const& config)
    : Action{action_context},
      send_confirm_repeat_{&send_confirm_repeat},
      send_ack_timeout_{config.send_ack_timeout},
      send_repeat_timeout_{config.send_repeat_timeout},
      window_size_{config.window_size},
      acknowledgement_req_{false} {}

UpdateStatus SafeStreamRecvAction::Update(TimePoint current_time) {
  CheckCompletedChains();
  return UpdateStatus::Merge(CheckAcknowledgement(current_time),
                             CheckMissing(current_time));
}

void SafeStreamRecvAction::PushData(SSRingIndex begin,
                                    DataMessage data_message) {
  AE_TELED_DEBUG(
      "Data received begin offset: {}, delta: {}, repeat {}, reset {}, size {}",
      begin, data_message.delta_offset,
      static_cast<int>(data_message.repeat_count()), data_message.reset(),
      data_message.data.size());

  // The first packet received, sync with the sender
  if (!begin_) {
    AE_TELED_DEBUG("Init receiver");
    session_start_ = begin;
    begin_ = begin;
    last_emitted_ = *begin_;
    // The sender sent first packet, sync with the sender
  } else if (data_message.reset() && (session_start_ != begin)) {
    AE_TELED_DEBUG("Reset receiver");
    session_start_ = begin;
    begin_ = begin;
    last_emitted_ = *begin_;
    chunks_.Clear();
    sent_ack_time_.reset();
    repeat_request_time_.reset();
    acknowledgement_req_ = false;
  }

  auto received_offset = begin + data_message.delta_offset;
  HandleData(received_offset, data_message.repeat_count(),
             std::move(data_message.data));
}

SafeStreamRecvAction::ReceiveEvent::Subscriber
SafeStreamRecvAction::receive_event() {
  return EventSubscriber{receive_event_};
}

void SafeStreamRecvAction::HandleData(SSRingIndex received_offset,
                                      std::uint8_t repeat_count,
                                      DataBuffer&& data) {
  assert(begin_);
  Action::Trigger();

  auto res = chunks_.AddChunk(
      ReceivingChunk{received_offset, std::move(data), repeat_count});
  switch (res) {
    case ReceiveChunkList::AddResult::kDuplicate: {
      AE_TELED_DEBUG("Received duplicate");
      break;
    }
    default:
      acknowledgement_req_ = true;
      break;
  }
}

void SafeStreamRecvAction::CheckCompletedChains() {
  if (!session_start_) {
    return;
  }
  auto joined_chunk = chunks_.ReceiveChunk(last_emitted_);
  if (joined_chunk) {
    last_emitted_ = joined_chunk->offset_range().right + 1;
    chunks_.Acknowledge(last_emitted_ - window_size_, last_emitted_);
    acknowledgement_req_ = true;

    AE_TELED_DEBUG("Emit received data range: {}-{}, last_emitted_: {}",
                   joined_chunk->offset_range().left,
                   joined_chunk->offset_range().right, last_emitted_);
    receive_event_.Emit(std::move(joined_chunk->data));
  }
}

UpdateStatus SafeStreamRecvAction::CheckAcknowledgement(
    TimePoint current_time) {
  if (!acknowledgement_req_) {
    sent_ack_time_.reset();
    return {};
  }

  if (!sent_ack_time_) {
    sent_ack_time_ = current_time + send_ack_timeout_;
  }

  if (*sent_ack_time_ > current_time) {
    return UpdateStatus::Delay(*sent_ack_time_);
  }
  sent_ack_time_.reset();
  acknowledgement_req_ = false;

  SendAcknowledgement(last_emitted_);
  Action::Trigger();

  return {};
}

UpdateStatus SafeStreamRecvAction::CheckMissing(TimePoint current_time) {
  if (chunks_.empty()) {
    repeat_request_time_.reset();
    return {};
  }

  if (!repeat_request_time_) {
    repeat_request_time_ = current_time + send_repeat_timeout_;
  }

  if (*repeat_request_time_ > current_time) {
    return UpdateStatus::Delay(*repeat_request_time_);
  }
  repeat_request_time_.reset();

  auto missed = chunks_.FindMissedChunks(last_emitted_);

  if (missed.empty()) {
    return {};
  }

  auto min = std::min_element(
      missed.begin(), missed.end(), [](const auto& left, const auto& right) {
        return left.expected_offset.IsBefore(right.expected_offset);
      });

  AE_TELED_DEBUG(
      "Request to repeat offset: {} for last_emitted: {} next_saved: {}",
      min->expected_offset, last_emitted_, min->chunk->offset);

  SendRequestRepeat(min->expected_offset);

  Action::Trigger();
  return {};
}

void SafeStreamRecvAction::SendAcknowledgement(SSRingIndex offset) {
  AE_TELED_DEBUG("Send acknowledgement for offset {}", offset);
  send_confirm_repeat_->SendAck(offset);
}

void SafeStreamRecvAction::SendRequestRepeat(SSRingIndex offset) {
  AE_TELED_DEBUG("Send request repeat for offset {}", offset);
  send_confirm_repeat_->SendRepeatRequest(offset);
}

}  // namespace ae
