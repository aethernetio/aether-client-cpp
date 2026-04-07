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
RepeatableTask::RepeatableTask(AeContext const& ae_context, Task&& task,
                               Duration interval, int max_repeat_count)
    : ae_context_{ae_context},
      task_{std::move(task)},
      interval_{interval},
      max_repeat_count_{max_repeat_count},
      current_repeat_{} {
  Run();
}

void RepeatableTask::Stop() { stopped_ = true; }

void RepeatableTask::Run() {
  if ((max_repeat_count_ != kRepeatCountInfinite) &&
      (current_repeat_ >= max_repeat_count_)) {
    repeat_count_exceeded_event_.Emit();
    return;
  }
  current_repeat_++;
  ae_context_.scheduler().DelayedTask(
      [&]() {
        if (stopped_) {
          return;
        }
        // execute task
        task_();
        // reschedule
        Run();
      },
      interval_);
}
}  // namespace ae
