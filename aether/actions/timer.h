/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_ACTIONS_TIMER_H_
#define AETHER_ACTIONS_TIMER_H_

#include <chrono>
#include <utility>

#include "aether/actions/action2_.h"
#include "aether/meta/time_traits.h"
#include "aether/types/small_function.h"
#include "aether/actions/action_context.h"

namespace ae {
template <ActionContext AC, typename Task = SmallFunction<void()>>
class Timer final : public a2::Action {
 public:
  Timer(AC const& ac, Task&& task,
        std::chrono::system_clock::time_point timeout)
      : Timer(std::in_place, ac, std::move(task), timeout) {}

  template <typename D>
    requires(IsDuration_v<D>)
  Timer(AC const& ac, Task&& task, D timeout)
      : Timer(std::in_place, ac, std::move(task), timeout) {}

  void Reset() { schedule_sub_.Reset(); }

 private:
  template <typename Timeout>
  Timer(std::in_place_t, AC const& ac, Task&& task, Timeout timeout)
      : ac_{ac},
        task_{std::move(task)},
        // schedule timer
        schedule_sub_{
            ac_.scheduler().DelayedTask([this]() { Invoke(); }, timeout)} {}

  void Invoke() {
    std::invoke(task_);
    Finish();
  }

  AC ac_;
  Task task_;
  TaskSubscription schedule_sub_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_TIMER_H_
