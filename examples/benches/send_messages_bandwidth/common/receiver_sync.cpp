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

#include "send_messages_bandwidth/common/receiver_sync.h"

#include <utility>

#include "aether/api_protocol/packet_builder.h"

#include "aether/tele/tele.h"

#include "send_messages_bandwidth/common/bandwidth_api.h"

namespace ae::bench {
ReceiverSyncAction::ReceiverSyncAction(ActionContext action_context,
                                       ProtocolContext& protocol_context,
                                       Ptr<ByteStream> stream)
    : Action{action_context},
      protocol_context_{protocol_context},
      stream_{std::move(stream)},
      state_{State::kWait},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })},
      received_sync_subscription_{
          protocol_context_
              .OnMessage<BandwidthApi::Sync>([this](auto const& msg) {
                OnReceivedSync(msg.message().request_id);
              })
              .Once()} {}

TimePoint ReceiverSyncAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWait:
        start_time_ = current_time;
        break;
      case State::kReceived:
        Action::Result(*this);
        break;
      case State::kError:
        Action::Error(*this);
        break;
    }
  }

  if (state_.get() == State::kWait) {
    return CheckTimeout(current_time);
  }

  return current_time;
}

void ReceiverSyncAction::OnReceivedSync(RequestId request_id) {
  AE_TELED_DEBUG("Received sync message");

  stream_->in().Write(PacketBuilder{protocol_context_,
                                    PackMessage{ReturnResultApi{},
                                                SendResult{request_id, true}}},
                      Now());

  state_.Set(State::kReceived);
}

TimePoint ReceiverSyncAction::CheckTimeout(TimePoint current_time) {
  if (current_time - start_time_ > kTimeout) {
    state_.Set(State::kError);
    return current_time;
  }
  return start_time_ + kTimeout;
}

}  // namespace ae::bench
