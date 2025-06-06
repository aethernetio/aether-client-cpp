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

#include "aether/tele/tele.h"

namespace ae {
SafeStreamRecvAction::SafeStreamRecvAction(
    ActionContext action_context, ISendConfirmRepeat& send_confirm_repeat)
    : Action{action_context}, send_confirm_repeat_{&send_confirm_repeat} {}

TimePoint SafeStreamRecvAction::Update(TimePoint current_time) {
  ChecklCompletedChains();
  auto new_time = current_time;
  for (auto const& t :
       {CheckConfirmations(current_time), CheckMissing(current_time)}) {
    if ((new_time == current_time) && (new_time != t)) {
      new_time = t;
    }
    if (t != current_time) {
      new_time = std::min(new_time, t);
    }
  }
  return new_time;
}

void SafeStreamRecvAction::PushData(DataBuffer&& data,
                                    SSRingIndex received_offset,
                                    std::uint16_t repeat_count) {
  auto data_size = data.size();
  AE_TELED_DEBUG("Data received offset: {}, repeat {}, size {}",
                 received_offset, repeat_count, data_size);
  if (begin_.Distance(received_offset) > window_size_) {
    AE_TELED_DEBUG("Received repeat for old chunk");
    return;
  }
  auto const res = chunks_.AddChunk(
      ReceivingChunk{received_offset, std::move(data), repeat_count},
      last_emitted_, begin_);
  if (!res || (res->repeat_count != repeat_count)) {
    // confirmed offset
    SendConfirmation(received_offset +
                     static_cast<SSRingIndex::type>(data_size - 1));
    return;
  }
  if (!sent_confirm_) {
    sent_confirm_ = Now();
  }
  if (!repeat_request_) {
    repeat_request_ = Now();
  }
  Action::Trigger();
}

void SafeStreamRecvAction::SetOffset(SSRingIndex offset) {
  begin_ = offset;
  last_emitted_ = offset;
  chunks_.Clear();
}

void SafeStreamRecvAction::SetConfig(SSRingIndex::type window_size,
                                     Duration send_confirm_timeout,
                                     Duration send_repeat_timeout) {
  window_size_ = window_size;
  send_confirm_timeout_ = send_confirm_timeout;
  send_repeat_timeout_ = send_repeat_timeout;
}

SafeStreamRecvAction::ReceiveEvent::Subscriber
SafeStreamRecvAction::receive_event() {
  return EventSubscriber{receive_event_};
}

void SafeStreamRecvAction::ChecklCompletedChains() {
  auto joined_chunk = chunks_.PopChunks(last_emitted_);
  if (joined_chunk) {
    last_emitted_ += static_cast<SSRingIndex::type>(joined_chunk->data.size());

    receive_event_.Emit(std::move(joined_chunk->data));
  }
}

TimePoint SafeStreamRecvAction::CheckConfirmations(TimePoint current_time) {
  if (!sent_confirm_) {
    return current_time;
  }
  if ((*sent_confirm_ + send_confirm_timeout_) > current_time) {
    return (*sent_confirm_ + send_confirm_timeout_);
  }

  if (last_emitted_(begin_) != begin_) {
    // confirm range [last_confirmed_offset_, last_emitted_offset_)
    SendConfirmation(last_emitted_ - 1);
    begin_ = last_emitted_;
    sent_confirm_ = current_time;
  }
  return current_time;
}

TimePoint SafeStreamRecvAction::CheckMissing(TimePoint current_time) {
  if (!repeat_request_) {
    return current_time;
  }
  if ((*repeat_request_ + send_repeat_timeout_) > current_time) {
    return *repeat_request_ + send_repeat_timeout_;
  }

  auto missed = chunks_.FindMissedChunks(last_emitted_);

  for (auto& m : missed) {
    ++m.chunk->repeat_count;
    // TODO: add check repeat count
    AE_TELED_DEBUG("Request to repeat offset: {}", m.expected_offset);
    SendRequestRepeat(m.expected_offset);
  }
  repeat_request_ = current_time;
  return current_time;
}

void SafeStreamRecvAction::SendConfirmation(SSRingIndex offset) {
  send_confirm_repeat_->SendConfirm(offset);
}

void SafeStreamRecvAction::SendRequestRepeat(SSRingIndex offset) {
  send_confirm_repeat_->SendRepeatRequest(offset);
}

}  // namespace ae
