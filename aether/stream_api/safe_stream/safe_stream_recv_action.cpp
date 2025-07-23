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
      send_ack_timeout_{config.send_confirm_timeout},
      send_repeat_timeout_{config.send_repeat_timeout} {}

ActionResult SafeStreamRecvAction::Update(TimePoint current_time) {
  ChecklCompletedChains();
  return ActionResult::Merge(CheckAcknowledgement(current_time),
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
    acknowledgement_queue_.clear();
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
  defer[this] { Action::Trigger(); };

  if (received_offset.IsBefore(last_emitted_)) {
    AE_TELED_DEBUG("Received repeat for old chunk");
    acknowledgement_queue_.push_back(last_emitted_);
    return;
  }

  chunks_.AddChunk(
      ReceivingChunk{received_offset, std::move(data), repeat_count},
      last_emitted_);
}

void SafeStreamRecvAction::ChecklCompletedChains() {
  if (!session_start_) {
    return;
  }
  auto joined_chunk = chunks_.PopChunks(last_emitted_);
  if (joined_chunk) {
    last_emitted_ += static_cast<SSRingIndex::type>(joined_chunk->data.size());
    AE_TELED_DEBUG("Emit received data range: {}-{}, last_emitted_: {}",
                   joined_chunk->offset_range().left,
                   joined_chunk->offset_range().right, last_emitted_);
    receive_event_.Emit(std::move(joined_chunk->data));
    acknowledgement_queue_.push_back(last_emitted_);
  }
}

ActionResult SafeStreamRecvAction::CheckAcknowledgement(
    TimePoint current_time) {
  if (acknowledgement_queue_.empty()) {
    sent_ack_time_.reset();
    return {};
  }

  if (!sent_ack_time_) {
    sent_ack_time_ = current_time + send_ack_timeout_;
  }

  if (*sent_ack_time_ > current_time) {
    return ActionResult::Delay(*sent_ack_time_);
  }
  sent_ack_time_.reset();

  // sent confirm timeout
  MakeAcknowledgement();
  Action::Trigger();

  return {};
}

ActionResult SafeStreamRecvAction::CheckMissing(TimePoint current_time) {
  if (chunks_.empty()) {
    repeat_request_time_.reset();
    return {};
  }

  if (!repeat_request_time_) {
    repeat_request_time_ = current_time + send_repeat_timeout_;
  }

  if (*repeat_request_time_ > current_time) {
    return ActionResult::Delay(*repeat_request_time_);
  }
  repeat_request_time_.reset();

  AE_TELED_DEBUG("Chunks on check missing: {}", chunks_.current_chunks());

  auto missed = chunks_.FindMissedChunks(last_emitted_);

  for (auto& m : missed) {
    ++m.chunk->repeat_count;
    // TODO: add check repeat count
    AE_TELED_DEBUG(
        "Request to repeat offset: {} for last_emitted: {} next_saved: {}",
        m.expected_offset, last_emitted_, m.chunk->offset);
    SendRequestRepeat(m.expected_offset);
  }
  Action::Trigger();
  return {};
}

void SafeStreamRecvAction::MakeAcknowledgement() {
  // get the maximum confirmation index
  auto max = std::max_element(
      std::begin(acknowledgement_queue_), std::end(acknowledgement_queue_),
      [](SSRingIndex left, SSRingIndex right) { return left.IsBefore(right); });
  assert(max != std::end(acknowledgement_queue_));
  SendAcknowledgement(*max);

  acknowledgement_queue_.clear();
  begin_ = last_emitted_;
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
