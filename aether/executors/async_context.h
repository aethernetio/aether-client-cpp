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

#ifndef AETHER_EXECUTORS_ASYNC_CONTEXT_H_
#define AETHER_EXECUTORS_ASYNC_CONTEXT_H_

#include <chrono>
#include <utility>
#include <type_traits>

#include "aether/tasks/details/task_subsctiption.h"

namespace ae::ex {
template <typename T>
concept AsyncScheduler = requires(T t) {
  TaskSubscription{t.Task([]() {})};
  TaskSubscription{t.DelayedTask(
      []() {}, std::declval<std::chrono::system_clock::time_point>())};
  TaskSubscription{
      t.DelayedTask([]() {}, std::declval<std::chrono::milliseconds>())};
};

/**
 * \brief AsyncContext is a concept for type with scheduler method returning
 * AsyncScheduler type
 */
template <typename T>
concept AsyncContext = requires(T t) {
  std::is_copy_constructible_v<T>;
  // has scheduler() method
  { t.scheduler() } -> AsyncScheduler;
};
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_ASYNC_CONTEXT_H_
