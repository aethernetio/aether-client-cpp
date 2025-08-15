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

#include "send_message_delays/timed_receiver.h"

#include "aether/tele/tele.h"

namespace ae::bench {
TimedReceiver::TimedReceiver(ActionContext action_context,
                             std::size_t wait_count)
    : Action{action_context},
      wait_count_{wait_count},
      state_{State::kWaiting},
      state_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELED_INFO("TimedReceiver waiting {} messages", wait_count);
}

UpdateStatus TimedReceiver::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaiting:
        next_receive_time_ = kWaitTimeout + Now();
        break;
      case State::kReceived:
        return UpdateStatus::Result();
      case State::kTimeOut:
        return UpdateStatus::Error();
    }
  }

  if (state_.get() == State::kWaiting) {
    return CheckTimeout(Now());
  }

  return {};
}

void TimedReceiver::Receive(std::uint16_t id) {
  AE_TELED_DEBUG("Message received id {}", static_cast<int>(id));

  auto [_, ok] = message_times_.emplace(id, HighResTimePoint::clock::now());
  if (!ok) {
    AE_TELED_ERROR("Duplicate message");
    return;
  }

  next_receive_time_ = kWaitTimeout + Now();
  auto last = id >= wait_count_;
  received_event_.Emit(last);

  if (last) {
    state_.Set(State::kReceived);
    return;
  }
}

UpdateStatus TimedReceiver::CheckTimeout(TimePoint current_time) {
  if (next_receive_time_ > current_time) {
    return UpdateStatus::Delay(next_receive_time_);
  }

  AE_TELED_ERROR("Receive message timeout");
  state_.Set(State::kTimeOut);
  return {};
}

}  // namespace ae::bench
