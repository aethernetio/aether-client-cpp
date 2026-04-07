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

#include "aether/common.h"
#include "aether/ae_context.h"
#include "aether/actions/action2_.h"
#include "aether/types/small_function.h"

namespace ae {
/**
 * \brief Runs Task periodically with interval_ until repeat count is exceeded
 * or stopped.
 */

class RepeatableTask : public a2::Action {
 public:
  using Task = SmallFunction<void()>;
  static constexpr auto kRepeatCountInfinite = -1;

  RepeatableTask(AeContext const& ae_context, Task&& task, Duration interval,
                 int max_repeat_count = kRepeatCountInfinite);

  AE_CLASS_MOVE_ONLY(RepeatableTask)

  void Stop();

  Event<void()>::Subscriber repeat_count_exceeded() noexcept {
    return EventSubscriber{repeat_count_exceeded_event_};
  }

 private:
  void Run();

  AeContext ae_context_;
  Task task_;
  Duration interval_;
  int max_repeat_count_;
  int current_repeat_;
  bool stopped_{false};
  Event<void()> repeat_count_exceeded_event_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_REPEATABLE_TASK_H_
