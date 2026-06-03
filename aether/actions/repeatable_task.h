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

#include <chrono>
#include <functional>

#include "aether/actions/action.h"
#include "aether/meta/time_traits.h"
#include "aether/types/small_function.h"
#include "aether/actions/action_context.h"

namespace ae {
/**
 * \brief Runs Task periodically with interval_ until repeat count is exceeded
 * or stopped.
 */
template <ActionContext AC, typename Task = SmallFunction<void()>>
class RepeatableTask : public Action {
 public:
  static constexpr auto kRepeatCountInfinite = -1;

  template <typename D>
    requires(IsDuration_v<D>)
  RepeatableTask(AC const& ac, Task&& task, D interval,
                 int max_repeat_count = kRepeatCountInfinite)
      : ac_{ac},
        task_{std::move(task)},
        interval_{
            std::chrono::duration_cast<std::chrono::milliseconds>(interval)},
        max_repeat_count_{max_repeat_count} {
    Run();
  }

  AE_CLASS_MOVE_ONLY(RepeatableTask)

  void Stop() { stopped_ = true; }

  Event<void()>::Subscriber repeat_count_exceeded() noexcept {
    return EventSubscriber{repeat_count_exceeded_event_};
  }

 private:
  void Run() {
    if ((max_repeat_count_ != kRepeatCountInfinite) &&
        (current_repeat_ >= max_repeat_count_)) {
      repeat_count_exceeded_event_.Emit();
      return;
    }
    current_repeat_++;
    scheduler_sub_ = ac_.scheduler().DelayedTask(
        [&]() {
          if (stopped_) {
            return;
          }
          // execute task
          std::invoke(task_);
          // reschedule
          Run();
        },
        interval_);
  }

  AC ac_;
  Task task_;
  std::chrono::milliseconds interval_{};
  int max_repeat_count_{};
  int current_repeat_{};
  bool stopped_{false};
  Event<void()> repeat_count_exceeded_event_;
  TaskSubscription scheduler_sub_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_REPEATABLE_TASK_H_
