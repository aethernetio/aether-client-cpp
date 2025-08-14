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

#ifndef AETHER_ACTIONS_REPEATABLE_TASK_H_
#define AETHER_ACTIONS_REPEATABLE_TASK_H_

#include <functional>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

namespace ae {
/**
 * \brief Runs Task periodically with interval_ until repeat count is exceeded
 * or stopped.
 */

class RepeatableTask : public Action<RepeatableTask> {
 public:
  enum class State : std::uint8_t {
    kRun,
    kWait,
    kStop,
    kRepeatCountExceeded,
  };

  using Task = std::function<void()>;
  static constexpr auto kRepeatCountInfinite = -1;

  RepeatableTask(ActionContext action_context, Task&& task, Duration interval,
                 int max_repeat_count = kRepeatCountInfinite);

  AE_CLASS_MOVE_ONLY(RepeatableTask)

  UpdateStatus Update(TimePoint current_time);

  void Stop();

 private:
  void Run(TimePoint current_time);
  UpdateStatus CheckInterval(TimePoint current_time);

  Task task_;
  Duration interval_;
  int max_repeat_count_;

  StateMachine<State> state_;
  TimePoint next_execution_time_;
  int current_repeat_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_REPEATABLE_TASK_H_
