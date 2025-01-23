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

#include "send_messages_bandwidth/common/sender_sync.h"

#include <utility>

#include "aether/api_protocol/packet_builder.h"

#include "aether/tele/tele.h"

#include "send_messages_bandwidth/common/bandwidth_api.h"

namespace ae::bench {
SenderSyncAction::SenderSyncAction(ActionContext action_context,
                                   ProtocolContext& protocol_context,
                                   Ptr<ByteStream> stream)
    : Action{action_context},
      protocol_context_{protocol_context},
      stream_{std::move(stream)},
      state_{State::kSendSync},
      current_repeat_{},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

TimePoint SenderSyncAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSendSync:
        SendSync(current_time);
        break;
      case State::kWaitResponse:
        break;
      case State::kSuccess:
        Action::Result(*this);
        break;
      case State::kError:
        Action::Error(*this);
        break;
    }
  }
  if (state_.get() == State::kWaitResponse) {
    current_time = CheckInterval(current_time);
  }
  return current_time;
}

void SenderSyncAction::SendSync(TimePoint current_time) {
  if (current_repeat_ >= kRepeatCount) {
    AE_TELED_ERROR("Repeat count exceeded");
    state_.Set(State::kError);
    return;
  }

  AE_TELED_DEBUG("Send sync request");

  auto request_id = RequestId::GenRequestId();
  protocol_context_.AddSendResultCallback(request_id,
                                          [this](ApiParser& parser) {
                                            if (parser.Extract<bool>()) {
                                              state_.Set(State::kSuccess);
                                            } else {
                                              state_.Set(State::kError);
                                            }
                                          });
  auto packet = PacketBuilder{
      protocol_context_,
      PackMessage{BandwidthApi{}, BandwidthApi::Sync{{}, request_id}}};
  stream_->in().Write(std::move(packet), current_time);
  send_time_ = current_time;
  current_repeat_++;
  state_.Set(State::kWaitResponse);
}

TimePoint SenderSyncAction::CheckInterval(TimePoint current_time) {
  if ((current_time - send_time_) > kSendInterval) {
    state_ = State::kSendSync;
    return current_time;
  }
  return send_time_ + kSendInterval;
}

}  // namespace ae::bench
