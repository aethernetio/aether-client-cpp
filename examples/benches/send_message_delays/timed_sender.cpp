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

#include "send_message_delays/timed_sender.h"

#include "aether/tele/tele.h"

namespace ae::bench {
TimedSender::TimedSender(ActionContext action_context,
                         std::function<void(std::uint16_t id)> send_proc,
                         Duration send_interval)
    : Action{action_context},
      send_proc_{std::move(send_proc)},
      send_interval_{send_interval},
      state_{State::kSend},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  AE_TELED_DEBUG("TimedSender with min send interval {} us",
                 send_interval_.count());
}

ActionResult TimedSender::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kSend:
        Send();
        break;
      case State::kFinished:
        return ActionResult::Result();
      case State::kError:
        return ActionResult::Error();
      default:
        break;
    }
  }

  if (state_.get() == State::kWaitSync) {
    return CheckSyncTimeout(Now());
  }

  return {};
}

void TimedSender::Stop() {
  AE_TELED_DEBUG("Stop sending");
  state_.Set(State::kFinished);
}

void TimedSender::Sync() {
  AE_TELED_DEBUG("Sync");
  state_.Set(State::kSend);
}

void TimedSender::Send() {
  AE_TELED_DEBUG("Send message {} ", static_cast<int>(current_id_));

  message_times_.emplace(current_id_, HighResTimePoint::clock::now());

  send_proc_(current_id_);

  next_send_time_ = send_interval_ + Now();

  ++current_id_;
  state_ = State::kWaitSync;
}

ActionResult TimedSender::CheckSyncTimeout(TimePoint current_time) {
  if (next_send_time_ > current_time) {
    return ActionResult::Delay(next_send_time_);
  }
  state_.Set(State::kSend);
  return {};
}
}  // namespace ae::bench
