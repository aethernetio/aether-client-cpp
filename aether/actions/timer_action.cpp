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

#include "aether/actions/timer_action.h"

namespace ae {
TimerAction::TimerAction(TimerAction&& other) noexcept
    : Action{std::move(other)},
      timer_duration_{other.timer_duration_},
      start_time_{other.start_time_},
      state_{std::move(other.state_)},
      state_changed_sub_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

TimerAction& TimerAction::operator=(TimerAction&& other) noexcept {
  if (this != &other) {
    Action::operator=(std::move(other));
    timer_duration_ = other.timer_duration_;
    start_time_ = other.start_time_;
    state_ = std::move(other.state_);
    state_changed_sub_ =
        state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  }
  return *this;
}

TimePoint TimerAction::Update(TimePoint current_time) {
  if (state_.get() == State::kWait) {
    if ((start_time_ + timer_duration_) > current_time) {
      return start_time_ + timer_duration_;
    }
    state_ = State::kTriggered;
  }
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kStart:
        start_time_ = current_time;
        state_ = State::kWait;
        break;
      case State::kWait:
        break;
      case State::kTriggered:
        Action::Result(*this);
        return current_time;
      case State::kStopped:
        Action::Stop(*this);
        return current_time;
    }
  }
  return current_time;
}

void TimerAction::Stop() { state_ = State::kStopped; }

}  // namespace ae
