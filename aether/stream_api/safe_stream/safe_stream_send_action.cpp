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

#include <cstdlib>

#include "aether/tele/tele.h"

namespace ae {
namespace safe_stream_send_action_internal {
SSRingIndex RandomOffset() {
  // set seed once
  static bool const seed =
      (std::srand(static_cast<unsigned int>(time(nullptr))), true);
  (void)seed;
  auto value = std::rand();
  return SSRingIndex{static_cast<SSRingIndex::type>(value)};
}
}  // namespace safe_stream_send_action_internal

SafeStreamSendAction::SafeStreamSendAction(ActionContext action_context,
                                           ISendDataPush& send_data_push,
                                           SafeStreamConfig const& config)
    : Action{action_context},
      send_data_push_{&send_data_push},
      begin_{safe_stream_send_action_internal::RandomOffset()},
      last_sent_{begin_},
      last_added_{begin_},
      init_state_{true},
      max_repeat_count_{config.max_repeat_count},
      max_payload_size_{0},
      window_size_{config.window_size},
      send_data_buffer_{action_context} {
  // add wait ack timeout as base value for response statistics
  response_statistics_.Add(config.wait_ack_timeout);
}

UpdateStatus SafeStreamSendAction::Update(TimePoint current_time) {
  SendChunk(current_time);
  return SendTimeouts(current_time);
}

bool SafeStreamSendAction::Acknowledge(SSRingIndex confirm_offset) {
  AE_TELED_DEBUG("Receive ack for offset {}", confirm_offset);
  if (begin_.IsAfter(confirm_offset)) {
    return false;
  }
  init_state_ = false;

  if (!sending_chunks_.empty()) {
    auto response_duration = std::chrono::duration_cast<Duration>(
        Now() - sending_chunks_.front().send_time);
    AE_TELED_DEBUG("Response duration is {:%S}", response_duration);
    response_statistics_.Add(response_duration);
  }

  sending_chunks_.RemoveUpTo(confirm_offset);
  send_data_buffer_.Acknowledge(confirm_offset);
  begin_ = confirm_offset;
  Action::Trigger();
  return true;
}

void SafeStreamSendAction::RequestRepeat(SSRingIndex request_offset) {
  if (last_sent_.IsBefore(request_offset)) {
    AE_TELED_DEBUG("Request repeat send for not sent offset {}",
                   request_offset);
    return;
  }
  last_sent_ = request_offset;
  Action::Trigger();
}

void SafeStreamSendAction::SetMaxPayload(std::size_t max_payload_size) {
  AE_TELED_DEBUG("Set max payload to {}", max_payload_size);
  max_payload_size_ = static_cast<SSRingIndex::type>(max_payload_size);
}

ActionPtr<SendingDataAction> SafeStreamSendAction::SendData(DataBuffer&& data) {
  auto data_size = data.size();
  auto sending_data = SendingData{last_added_, std::move(data)};
  last_added_ += static_cast<SSRingIndex::type>(data_size);

  auto send_action = send_data_buffer_.AddData(std::move(sending_data));
  sending_data_subs_.Push(
      // stop data chunks sending if main action is stopped
      send_action->stop_event().Subscribe([this](auto const& sending_data) {
        auto removed_size = send_data_buffer_.Stop(sending_data.offset);
        last_added_ -= static_cast<SSRingIndex::type>(removed_size);
      }));
  return send_action;
}

void SafeStreamSendAction::SendChunk(TimePoint current_time) {
  if (max_payload_size_ == 0) {
    return;
  }

  auto data_chunk = send_data_buffer_.GetSlice(last_sent_, max_payload_size_);
  if (data_chunk.data.empty()) {
    // no data to send
    return;
  }
  auto delta = begin_.Distance(data_chunk.offset);
  auto delta_end =
      delta + static_cast<SSRingIndex::type>(data_chunk.data.size());
  if (delta_end > window_size_) {
    AE_TELED_DEBUG("Window size exceeded begin: {} last_sent: {} delta end: {}",
                   begin_, last_sent_, delta_end);
    return;
  }
  AE_TELED_DEBUG("Sending chunk begin: {}, last_sent_: {},offset: {} size: {}",
                 begin_, last_sent_, data_chunk.offset, data_chunk.data.size());
  last_sent_ = begin_ + delta_end;

  auto& send_chunk = sending_chunks_.Register(
      data_chunk.offset,
      data_chunk.offset +
          static_cast<SSRingIndex::type>(data_chunk.data.size() - 1),
      current_time);

  auto repeat_count = send_chunk.repeat_count;
  send_chunk.repeat_count++;
  if (send_chunk.repeat_count > max_repeat_count_) {
    AE_TELED_ERROR("Repeat count exceeded");
    RejectSend(send_chunk);
    return;
  }

  auto end_offset = data_chunk.offset +
                    static_cast<SSRingIndex::type>(data_chunk.data.size());

  auto write_action = PushData(std::move(data_chunk.data), delta, repeat_count);

  send_subs_.Push(write_action->StatusEvent().Subscribe(
      ActionHandler{OnError{[this, end_offset]() {
                      sending_chunks_.RemoveUpTo(end_offset);
                      send_data_buffer_.Reject(end_offset);
                    }},
                    OnStop{[this, end_offset](auto const&) {
                      sending_chunks_.RemoveUpTo(end_offset);
                      send_data_buffer_.Stop(end_offset);
                    }}}));
}

void SafeStreamSendAction::RejectSend(SendingChunk& sending_chunk) {
  auto end_offset = sending_chunk.offset_range.right;
  sending_chunks_.RemoveUpTo(end_offset);
  send_data_buffer_.Reject(end_offset);
}

UpdateStatus SafeStreamSendAction::SendTimeouts(TimePoint current_time) {
  if (sending_chunks_.empty()) {
    return {};
  }

  auto const& selected_sch = sending_chunks_.front();
  // wait timeout is depends on repeat_count and response statistics
  auto wait_ack_timeout =
      static_cast<double>(response_statistics_.percentile<99>().count());
  auto increase_factor = std::max(
      1.0, (AE_SAFE_STREAM_RTO_GROW_FACTOR * (selected_sch.repeat_count - 1)));
  auto wait_timeout =
      Duration{static_cast<Duration::rep>(wait_ack_timeout * increase_factor)};

  auto wait_time = selected_sch.send_time + wait_timeout;
  if (wait_time <= current_time) {
    // timeout
    AE_TELED_DEBUG("Wait ack timeout {:%S}, repeat offset {}", wait_timeout,
                   selected_sch.offset_range.left);
    // move offset to repeat send
    last_sent_ = selected_sch.offset_range.left;
    Action::Trigger();
    return {};
  }

  return UpdateStatus::Delay(wait_time);
}

ActionPtr<StreamWriteAction> SafeStreamSendAction::PushData(
    DataBuffer&& data_buffer, SSRingIndex::type delta,
    std::uint8_t repeat_count) {
  AE_TELED_DEBUG(
      "Send data message begin offset {} delta {} repeat count {} reset {} "
      "data size {}",
      begin_, delta, static_cast<int>(repeat_count), init_state_,

      data_buffer.size());
  assert(delta < window_size_);

  return send_data_push_->PushData(begin_,
                                   DataMessage{
                                       repeat_count,
                                       init_state_,
                                       static_cast<std::uint16_t>(delta),
                                       std::move(std::move(data_buffer)),
                                   });
}

}  // namespace ae
