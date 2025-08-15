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

#include "test-stream/mock_bad_streams.h"

#include <random>

#include "aether/tele/tele.h"

namespace ae {
namespace bad_streams_internal {
float RandomPercent() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<float> dis(0.0f, 1.0f);

  return dis(gen);
}

bool IsHitTheRate(float hit_rate) { return RandomPercent() < hit_rate; }

DoneStreamWriteAction::DoneStreamWriteAction(ActionContext action_context)
    : StreamWriteAction{action_context} {
  state_ = State::kDone;
}

}  // namespace bad_streams_internal

LostPacketsStream::LostPacketsStream(ActionContext action_context,
                                     float loss_rate)
    : action_context_{action_context}, loss_rate_{loss_rate} {}

ActionPtr<StreamWriteAction> LostPacketsStream::Write(
    DataBuffer&& data_buffer) {
  if (bad_streams_internal::IsHitTheRate(loss_rate_)) {
    AE_TELED_DEBUG("Packet loss!");
    // drop the packet
    // return data sent is done
    return ActionPtr<bad_streams_internal::DoneStreamWriteAction>{
        action_context_};
  }
  assert(out_);
  return out_->Write(std::move(data_buffer));
}

void LostPacketsStream::LinkOut(ByteIStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
  stream_update_event_.Emit();
}

PacketDelayStream::PacketDelayAction::PacketDelayAction(
    ActionContext action_context, ByteIStream& out, DataBuffer&& data_buffer,
    Duration duration)
    : Action{action_context},
      out_{&out},
      data_buffer_{std::move(data_buffer)},
      timeout_{duration} {}

UpdateStatus PacketDelayStream::PacketDelayAction::Update(
    TimePoint current_time) {
  if (!send_time_) {
    send_time_ = current_time + timeout_;
  }
  // wait time
  if (*send_time_ > current_time) {
    return UpdateStatus::Delay(*send_time_);
  }

  // send data
  AE_TELED_DEBUG("Delayed packet send!");
  out_->Write(std::move(data_buffer_));
  return UpdateStatus::Result();
}

PacketDelayStream::PacketDelayStream(ActionContext action_context,
                                     float delay_rate, Duration max_delay)
    : action_context_{action_context},
      delay_rate_{delay_rate},
      max_delay_{max_delay} {}

ActionPtr<StreamWriteAction> PacketDelayStream::Write(
    DataBuffer&& data_buffer) {
  assert(out_);

  if (bad_streams_internal::IsHitTheRate(delay_rate_)) {
    AE_TELED_DEBUG("Packet delay!");
    // delay send data by packet delay action
    auto packet_delay_action = ActionPtr<PacketDelayAction>{
        action_context_, *out_, std::move(data_buffer),
        std::chrono::duration_cast<Duration>(
            bad_streams_internal::RandomPercent() * max_delay_)};
    // return data sent is done
    return ActionPtr<bad_streams_internal::DoneStreamWriteAction>{
        action_context_};
  }

  return out_->Write(std::move(data_buffer));
}

void PacketDelayStream::LinkOut(ByteIStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(
      out_data_event_, MethodPtr<&OutDataEvent::Emit>{});
  stream_update_event_.Emit();
}

}  // namespace ae
