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

#include "mock_bad_streams.h"

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

DoneStreamWriteAction::DoneStreamWriteAction(AeContext const& ae_context) {
  ae_context.scheduler().Task([&]() { SetStatus(Status::kSuccess); });
}

}  // namespace bad_streams_internal

LostPacketsStream::LostPacketsStream(AeContext const& ae_context,
                                     float loss_rate)
    : ae_context_{ae_context}, loss_rate_{loss_rate} {}

WriteAction& LostPacketsStream::Write(DataBuffer&& data_buffer) {
  if (bad_streams_internal::IsHitTheRate(loss_rate_)) {
    AE_TELED_DEBUG("Packet loss!");
    // drop the packet
    // return data sent is done
    if (!dsw_ || dsw_->is_finished()) {
      dsw_.emplace(ae_context_);
    }
    return *dsw_;
  }
  assert(out_);
  return out_->Write(std::move(data_buffer));
}

void LostPacketsStream::LinkOut(ByteIStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(out_data_event_);
  stream_update_event_.Emit();
}

PacketDelayStream::PacketDelayStream(AeContext const& ae_context,
                                     float delay_rate, Duration max_delay)
    : ae_context_{ae_context}, delay_rate_{delay_rate}, max_delay_{max_delay} {}

WriteAction& PacketDelayStream::Write(DataBuffer&& data_buffer) {
  assert(out_);

  if (bad_streams_internal::IsHitTheRate(delay_rate_)) {
    AE_TELED_DEBUG("Packet delay!");
    // delay send data by packet delay action
    data_queue_.push(std::move(data_buffer));
    ae_context_.scheduler().DelayedTask(
        [this]() mutable {
          AE_TELED_DEBUG("Delayed packet send!");
          auto d = std::move(data_queue_.front());
          data_queue_.pop();
          out_->Write(std::move(d));
        },
        std::chrono::duration_cast<Duration>(
            bad_streams_internal::RandomPercent() * max_delay_));

    // return data sent is done
    if (!dsw_ || dsw_->is_finished()) {
      dsw_.emplace(ae_context_);
    }
    return *dsw_;
  }

  return out_->Write(std::move(data_buffer));
}

void PacketDelayStream::LinkOut(ByteIStream& out) {
  out_ = &out;
  out_data_sub_ = out_->out_data_event().Subscribe(out_data_event_);
  stream_update_event_.Emit();
}

}  // namespace ae
