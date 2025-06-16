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

#include "aether/stream_api/safe_stream/safe_stream_send_action.h"

#include "aether/tele/tele.h"

namespace ae {
SafeStreamSendAction::SafeStreamSendAction(ActionContext action_context,
                                           ISendDataPush& send_data_push)
    : Action{action_context},
      send_data_push_{&send_data_push},
      send_data_buffer_{action_context} {}

ActionResult SafeStreamSendAction::Update(TimePoint current_time) {
  SendChunk();
  return ActionResult::Delay(SendTimeouts(current_time));
}

bool SafeStreamSendAction::Confirm(SSRingIndex confirm_offset) {
  AE_TELED_DEBUG("Receive confirmed offset {}", confirm_offset);
  if (begin_.Distance(confirm_offset) >= window_size_) {
    return false;
  }
  sending_chunks_.RemoveUpTo(confirm_offset, begin_);
  auto confirm_size = send_data_buffer_.Confirm(confirm_offset, begin_);
  begin_ += static_cast<SSRingIndex::type>(confirm_size);
  Action::Trigger();
  return true;
}

void SafeStreamSendAction::RequestRepeat(SSRingIndex request_offset) {
  if (last_sent_(begin_) < request_offset) {
    AE_TELED_DEBUG("Request repeat send for not sent offset {}",
                   request_offset);
  }
  last_sent_ = request_offset;
  Action::Trigger();
}

void SafeStreamSendAction::SetOffset(SSRingIndex begin_offset) {
  auto send_distance = begin_.Distance(begin_offset);
  begin_ = begin_offset;
  last_added_ += send_distance;
  last_sent_ += send_distance;
  send_data_buffer_.MoveOffset(send_distance);
  sending_chunks_.MoveOffset(send_distance);
}

void SafeStreamSendAction::SetConfig(std::uint16_t max_repeat_count,
                                     std::size_t max_packet_size,
                                     SSRingIndex::type window_size,
                                     Duration wait_confirm_timeout) {
  max_repeat_count_ = max_repeat_count;
  max_packet_size_ = static_cast<SSRingIndex::type>(max_packet_size);
  window_size_ = window_size;
  wait_confirm_timeout_ = wait_confirm_timeout;
}

ActionView<SendingDataAction> SafeStreamSendAction::SendData(
    DataBuffer&& data) {
  auto data_size = data.size();
  auto sending_data = SendingData{last_added_, std::move(data)};
  last_added_ += static_cast<SSRingIndex::type>(data_size);

  auto send_action = send_data_buffer_.AddData(std::move(sending_data));
  sending_data_subs_.Push(
      send_action->stop_event().Subscribe([this](auto const& sending_data) {
        auto removed_size = send_data_buffer_.Stop(sending_data.offset, begin_);
        last_added_ -= static_cast<SSRingIndex::type>(removed_size);
      }));
  return send_action;
}

void SafeStreamSendAction::SendChunk() {
  if (max_packet_size_ == 0) {
    return;
  }
  if (begin_.Distance(last_sent_ + max_packet_size_) > window_size_) {
    AE_TELED_DEBUG("Window size exceeded");
    return;
  }

  auto data_chunk =
      send_data_buffer_.GetSlice(last_sent_, max_packet_size_, begin_);
  if (data_chunk.data.empty()) {
    // no data to send
    return;
  }

  last_sent_ = data_chunk.offset +
               static_cast<SSRingIndex::type>(data_chunk.data.size());

  auto& send_chunk = sending_chunks_.Register(
      data_chunk.offset,
      data_chunk.offset +
          static_cast<SSRingIndex::type>(data_chunk.data.size() - 1),
      Now(), begin_);

  auto repeat_count = send_chunk.repeat_count;
  send_chunk.repeat_count++;
  if (send_chunk.repeat_count > max_repeat_count_) {
    AE_TELED_ERROR("Repeat count exceeded");
    RejectSend(send_chunk);
    return;
  }

  auto end_offset = data_chunk.offset +
                    static_cast<SSRingIndex::type>(data_chunk.data.size());

  auto write_action = PushData(std::move(data_chunk), repeat_count);

  send_subs_.Push(
      write_action->ErrorEvent().Subscribe([this, end_offset](auto const&) {
        sending_chunks_.RemoveUpTo(end_offset, begin_);
        send_data_buffer_.Reject(end_offset, begin_);
      }),
      write_action->StopEvent().Subscribe([this, end_offset](auto const&) {
        sending_chunks_.RemoveUpTo(end_offset, begin_);
        send_data_buffer_.Stop(end_offset, begin_);
      }));
}

void SafeStreamSendAction::RejectSend(SendingChunk& sending_chunk) {
  auto end_offset = sending_chunk.end_offset;
  sending_chunks_.RemoveUpTo(end_offset, begin_);
  send_data_buffer_.Reject(end_offset, begin_);
}

TimePoint SafeStreamSendAction::SendTimeouts(TimePoint current_time) {
  if (sending_chunks_.empty()) {
    return current_time;
  }

  auto const& selected_sch = sending_chunks_.front();
  // wait timeout is depends on repeat_count
  auto d_wait_confirm_timeout =
      static_cast<double>(wait_confirm_timeout_.count());
  auto increase_factor = std::max(
      1.0, (AE_SAFE_STREAM_RTO_GROW_FACTOR * (selected_sch.repeat_count - 1)));
  auto wait_timeout = Duration{
      static_cast<Duration::rep>(d_wait_confirm_timeout * increase_factor)};

  if ((selected_sch.send_time + wait_timeout) < current_time) {
    // timeout
    AE_TELED_DEBUG("Wait confirm timeout {:%S}, repeat offset {}", wait_timeout,
                   selected_sch.begin_offset);
    // move offset to repeat send
    last_sent_ = selected_sch.begin_offset;
    Action::Trigger();
    return current_time;
  }

  return selected_sch.send_time + wait_timeout;
}

ActionView<StreamWriteAction> SafeStreamSendAction::PushData(
    DataChunk&& data_chunk, std::uint16_t repeat_count) {
  return send_data_push_->PushData(std::move(data_chunk), repeat_count);
}

}  // namespace ae
