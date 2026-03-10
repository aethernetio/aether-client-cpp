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

#ifndef AETHER_TASKS_DETAILS_GENERIC_TASK_H_
#define AETHER_TASKS_DETAILS_GENERIC_TASK_H_

#include <utility>
#include <concepts>
#include <functional>

#include "aether/tasks/details/task.h"

namespace ae {
template <typename F>
  requires(std::invocable<F>)
class GenericTask : public ITask {
 public:
  explicit constexpr GenericTask(F&& f) : f_{std::move(f)} {}

  void Invoke() override { std::invoke(f_); }

 private:
  F f_;
};

template <typename F>
  requires(std::invocable<F>)
class GenericDelayedTask : public IDelayedTask {
 public:
  explicit constexpr GenericDelayedTask(F&& f, TimePoint tp)
      : IDelayedTask{tp}, f_{std::move(f)} {}

  void Invoke() override { std::invoke(f_); }

 private:
  F f_;
};

}  // namespace ae

#endif  // AETHER_TASKS_DETAILS_GENERIC_TASK_H_
