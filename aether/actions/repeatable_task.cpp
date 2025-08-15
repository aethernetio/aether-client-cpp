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

#include "aether/actions/repeatable_task.h"

namespace ae {
RepeatableTask::RepeatableTask(ActionContext action_context, Task&& task,
                               Duration interval, int max_repeat_count)
    : Action{action_context},
      task_{std::move(task)},
      interval_{interval},
      max_repeat_count_{max_repeat_count},
      state_{State::kRun},
      current_repeat_{} {}

UpdateStatus RepeatableTask::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kRun:
        Run(current_time);
        break;
      case State::kWait:
        break;
      case State::kStop:
        return UpdateStatus::Stop();
      case State::kRepeatCountExceeded:
        return UpdateStatus::Error();
    }
  }

  if (state_ == State::kWait) {
    return CheckInterval(current_time);
  }
  return {};
}

void RepeatableTask::Stop() {
  state_ = State::kStop;
  Action::Trigger();
}

void RepeatableTask::Run(TimePoint current_time) {
  if ((max_repeat_count_ != kRepeatCountInfinite) &&
      (current_repeat_ >= max_repeat_count_)) {
    state_ = State::kRepeatCountExceeded;
    Action::Trigger();
    return;
  }
  current_repeat_++;
  next_execution_time_ = current_time + interval_;
  // execute task
  task_();
  state_ = State::kWait;
  Action::Trigger();
}

UpdateStatus RepeatableTask::CheckInterval(TimePoint current_time) {
  if (next_execution_time_ <= current_time) {
    // repeat run
    state_ = State::kRun;
    Action::Trigger();
    return {};
  }
  return UpdateStatus::Delay(next_execution_time_);
}

}  // namespace ae
