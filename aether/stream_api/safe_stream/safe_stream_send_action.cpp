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

#include <random>

#include "aether/tele/tele.h"

namespace ae {
namespace safe_stream_send_action_internal {
auto GetRand() {
  static std::random_device dev;
  static std::mt19937 rng(dev());
  static std::uniform_int_distribution<std::mt19937::result_type> dist6(
      1, std::numeric_limits<std::uint32_t>::max());
  return dist6(rng);
}

SSRingIndex RandomOffset() {
  return SSRingIndex{static_cast<SSRingIndex::type>(
      GetRand() % std::numeric_limits<std::uint16_t>::max())};
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
      wait_confirm_timeout_{config.wait_confirm_timeout},
      send_data_buffer_{action_context} {}

ActionResult SafeStreamSendAction::Update(TimePoint current_time) {
  SendChunk(current_time);
  return SendTimeouts(current_time);
}

bool SafeStreamSendAction::Acknowledge(SSRingIndex confirm_offset) {
  AE_TELED_DEBUG("Receive ack for offset {}", confirm_offset);
  if (begin_.IsAfter(confirm_offset)) {
    return false;
  }
  init_state_ = false;
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

ActionView<SendingDataAction> SafeStreamSendAction::SendData(
    DataBuffer&& data) {
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

  send_subs_.Push(
      write_action->ErrorEvent().Subscribe([this, end_offset](auto const&) {
        sending_chunks_.RemoveUpTo(end_offset);
        send_data_buffer_.Reject(end_offset);
      }),
      write_action->StopEvent().Subscribe([this, end_offset](auto const&) {
        sending_chunks_.RemoveUpTo(end_offset);
        send_data_buffer_.Stop(end_offset);
      }));
}

void SafeStreamSendAction::RejectSend(SendingChunk& sending_chunk) {
  auto end_offset = sending_chunk.offset_range.right;
  sending_chunks_.RemoveUpTo(end_offset);
  send_data_buffer_.Reject(end_offset);
}

ActionResult SafeStreamSendAction::SendTimeouts(TimePoint current_time) {
  if (sending_chunks_.empty()) {
    return {};
  }

  auto const& selected_sch = sending_chunks_.front();
  // wait timeout is depends on repeat_count
  auto d_wait_confirm_timeout =
      static_cast<double>(wait_confirm_timeout_.count());
  auto increase_factor = std::max(
      1.0, (AE_SAFE_STREAM_RTO_GROW_FACTOR * (selected_sch.repeat_count - 1)));
  auto wait_timeout = Duration{
      static_cast<Duration::rep>(d_wait_confirm_timeout * increase_factor)};

  auto wait_time = selected_sch.send_time + wait_timeout;
  if (wait_time <= current_time) {
    // timeout
    AE_TELED_DEBUG("Wait confirm timeout {:%S}, repeat offset {}", wait_timeout,
                   selected_sch.offset_range.left);
    // move offset to repeat send
    last_sent_ = selected_sch.offset_range.left;
    Action::Trigger();
    return {};
  }

  return ActionResult::Delay(wait_time);
}

ActionView<StreamWriteAction> SafeStreamSendAction::PushData(
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
