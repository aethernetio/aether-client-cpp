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

#include "aether/stream_api/safe_stream/safe_stream_sending.h"

#include <utility>
#include <algorithm>

#include "aether/tele/tele.h"

namespace ae {
SafeStreamSendingAction::SafeStreamSendingAction(ActionContext action_context,
                                                 SafeStreamConfig const& config)
    : Action{action_context},
      buffer_capacity_{config.buffer_capacity},
      window_size_{config.window_size},
      max_repeat_count_{config.max_repeat_count},
      wait_confirm_timeout_{config.wait_confirm_timeout},
      max_data_size_{},
      send_data_buffer_{action_context, window_size_},
      sending_chunks_{window_size_},
      last_confirmed_{},
      next_to_add_{},
      last_sent_offset_{} {}

SafeStreamSendingAction::~SafeStreamSendingAction() = default;

TimePoint SafeStreamSendingAction::Update(TimePoint current_time) {
  auto new_time = HandleTimeouts(current_time);

  if (max_data_size_ != 0) {
    SendData(current_time);
  }
  return std::max(new_time, current_time + wait_confirm_timeout_);
}

SafeStreamSendingAction::SendEvent::Subscriber
SafeStreamSendingAction::send_event() {
  return send_event_;
}

SafeStreamSendingAction::RepeatEvent::Subscriber
SafeStreamSendingAction::repeat_event() {
  return repeat_event_;
}

ActionView<SendingDataAction> SafeStreamSendingAction::SendData(
    DataBuffer data) {
  if ((send_data_buffer_.size() + data.size()) > buffer_capacity_) {
    // buffer is full
    return {};
  }

  auto offset = next_to_add_;

  auto sending_data = SendingData{offset, std::move(data)};
  next_to_add_ = sending_data.get_offset_range(window_size_).end + 1;

  auto sending_action = send_data_buffer_.AddData(std::move(sending_data));

  send_data_subscriptions_.Push(sending_action->stop_event().Subscribe(
      [this, offset]() { StopSending(offset); }));

  Action::Trigger();

  return sending_action;
}

void SafeStreamSendingAction::Confirm(SafeStreamRingIndex offset) {
  auto distance = last_confirmed_.Distance(offset);
  if (distance <= window_size_) {
    ConfirmDataChunks(offset);
    last_confirmed_ = offset + 1;
  }
  AE_TELED_DEBUG("Receive confirmed offset {}", offset);
  Action::Trigger();
}

void SafeStreamSendingAction::RequestRepeatSend(SafeStreamRingIndex offset) {
  if (offset.Distance(last_sent_offset_) > window_size_) {
    AE_TELED_DEBUG("Request repeat send for to old offset {}",
                   last_sent_offset_);
  }
  last_sent_offset_ = offset;
  Action::Trigger();
}

void SafeStreamSendingAction::ReportWriteSuccess(
    SafeStreamRingIndex /* offset */) {}

void SafeStreamSendingAction::ReportWriteStopped(SafeStreamRingIndex offset) {
  AE_TELED_DEBUG("StopSending for offset:{}", offset);

  sending_chunks_.RemoveUpTo(offset);
  send_data_buffer_.Reject(offset);
}

void SafeStreamSendingAction::ReportWriteError(SafeStreamRingIndex offset) {
  AE_TELED_ERROR("Send error for offset:{}", offset);

  sending_chunks_.RemoveUpTo(offset);
  send_data_buffer_.Reject(offset);
}

void SafeStreamSendingAction::set_max_data_size(std::size_t max_data_size) {
  max_data_size_ = static_cast<SafeStreamRingIndex::type>(max_data_size);
  AE_TELED_DEBUG("Set max data size to {}", max_data_size_);
  Action::Trigger();
}

TimePoint SafeStreamSendingAction::HandleTimeouts(TimePoint current_time) {
  if (sending_chunks_.empty()) {
    return current_time;
  }

  auto const& selected_sch = sending_chunks_.front();
  // wait timeout is depends on repeat_count
  auto wait_timeout = Duration{static_cast<Duration::rep>(
      static_cast<double>(wait_confirm_timeout_.count()) *
      std::max(1.0,
               (AE_SAFE_STREAM_RTO_GROW_FACTOR * selected_sch.repeat_count)))};

  if ((selected_sch.send_time + wait_timeout) < current_time) {
    // timeout
    AE_TELED_DEBUG("Wait confirm timeout {:%S}, repeat offset {}", wait_timeout,
                   selected_sch.begin_offset);
    // move offset to repeat send
    last_sent_offset_ = selected_sch.begin_offset;
    return current_time;
  }

  return selected_sch.send_time + wait_timeout;
}

void SafeStreamSendingAction::SendData(TimePoint current_time) {
  if (last_confirmed_.Distance(last_sent_offset_ + max_data_size_) >
      window_size_) {
    AE_TELED_WARNING("Window size exceeded");
    return;
  }

  auto data_chunk =
      send_data_buffer_.GetSlice(last_sent_offset_, max_data_size_);
  if (data_chunk.data.empty()) {
    // no data to send
    return;
  }

  last_sent_offset_.Clockwise(
      static_cast<SafeStreamRingIndex::type>(data_chunk.data.size()));

  auto& send_chunk = sending_chunks_.Register(
      data_chunk.offset,
      data_chunk.offset +
          static_cast<SafeStreamRingIndex::type>(data_chunk.data.size() - 1),
      current_time);

  if (send_chunk.repeat_count == 0) {
    send_chunk.repeat_count += 1;
    SendFirst(std::move(data_chunk), current_time);
  } else {
    if ((send_chunk.repeat_count) > max_repeat_count_) {
      AE_TELED_ERROR("Repeat count exceeded");
      send_data_buffer_.Reject(send_chunk.end_offset);
      sending_chunks_.RemoveUpTo(send_chunk.end_offset);
      return;
    }
    send_chunk.repeat_count += 1;
    SendRepeat(std::move(data_chunk), send_chunk.repeat_count, current_time);
  }
}

void SafeStreamSendingAction::SendFirst(DataChunk&& chunk,
                                        TimePoint current_time) {
  AE_TELED_DEBUG("SendFirst chunk offset:{}", chunk.offset);
  send_event_.Emit(chunk.offset, std::move(chunk.data), current_time);
}

void SafeStreamSendingAction::SendRepeat(DataChunk&& chunk,
                                         std::uint16_t repeat_count,
                                         TimePoint current_time) {
  AE_TELED_DEBUG("SendRepeat chunk offset:{} count:{}", chunk.offset,
                 repeat_count);
  repeat_event_.Emit(chunk.offset, repeat_count, std::move(chunk.data),
                     current_time);
}

void SafeStreamSendingAction::ConfirmDataChunks(SafeStreamRingIndex offset) {
  sending_chunks_.RemoveUpTo(offset);
  send_data_buffer_.Confirm(offset);
}

void SafeStreamSendingAction::StopSending(SafeStreamRingIndex offset) {
  AE_TELED_DEBUG("StopSending offset:{}", offset);
  sending_chunks_.RemoveUpTo(offset);
  send_data_buffer_.Stop(offset);
}

}  // namespace ae
