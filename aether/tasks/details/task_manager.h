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

#ifndef AETHER_TASKS_DETAILS_TASK_MANAGER_H_
#define AETHER_TASKS_DETAILS_TASK_MANAGER_H_

#include <chrono>
#include <utility>
#include <concepts>

#include "aether/meta/time_traits.h"
#include "aether/tasks/details/task_queues.h"
#include "aether/tasks/details/generic_task.h"

#include "third_party/etl/include/etl/generic_pool.h"

namespace ae {
namespace task_manager_internal {
using DefTpType = std::chrono::system_clock::time_point;
static constexpr std::size_t kDefaultTaskSize =
    (6 * sizeof(void*)) +
    std::max(sizeof(ITask), sizeof(IDelayedTask<DefTpType>));
static constexpr std::size_t kDefaultTaskAlign =
    std::max(alignof(ITask), alignof(IDelayedTask<DefTpType>));
}  // namespace task_manager_internal

template <std::size_t Capacity,
          std::size_t ElementSize = task_manager_internal::kDefaultTaskSize,
          std::size_t ElementAlign = task_manager_internal::kDefaultTaskAlign>
struct TaskManagerConf {
  static constexpr std::size_t capacity = Capacity;
  static constexpr std::size_t element_size = ElementSize;
  static constexpr std::size_t element_align = ElementAlign;
};

template <typename TaskManagerConf,
          typename TimePointType = task_manager_internal::DefTpType>
class TaskManager {
  static constexpr std::size_t capacity = TaskManagerConf::capacity;
  static constexpr std::size_t element_size = TaskManagerConf::element_size;
  static constexpr std::size_t element_align = TaskManagerConf::element_align;

  using task_pool = etl::generic_pool<element_size, element_align, capacity>;
  using regular_task_list = TaskQueue<capacity, task_pool>;
  using delayd_task_list = DelayedTaskQueue<capacity, TimePointType, task_pool>;

 public:
  TaskManager()
      : regular_task_list_{task_pool_}, delayd_task_list_{task_pool_} {}

  /**
   * \brief Add regular task
   */
  template <typename F>
    requires(std::invocable<F>)
  IActive* Task(F&& f) {
    return Emplace<GenericTask<std::decay_t<F>>>(regular_task_list_,
                                                 std::forward<F>(f));
  }

  /**
   * \brief Add delayed task
   */
  template <typename F, typename Dur>
    requires(std::invocable<F> && IsDuration_v<Dur>)
  IActive* DelayedTask(F&& f, Dur dur) {
    return Emplace<GenericDelayedTask<std::decay_t<F>, TimePointType>>(
        delayd_task_list_, std::forward<F>(f),
        TimePointType::clock::now() + dur);
  }
  template <typename F>
    requires(std::invocable<F>)
  IActive* DelayedTask(F&& f, TimePointType tp) {
    return Emplace<GenericDelayedTask<std::decay_t<F>, TimePointType>>(
        delayd_task_list_, std::forward<F>(f), tp);
  }

  regular_task_list& regular() { return regular_task_list_; }
  delayd_task_list& delayed() { return delayd_task_list_; }

 private:
  template <typename T, typename List, typename... Args>
  IActive* Emplace(List& list, Args&&... args) {
    if (task_pool_.available() == 0) {
      return nullptr;
    }
    auto* p = task_pool_.template create<T>(std::forward<Args>(args)...);
    assert(p != nullptr);
    if (!list.Add(p)) {
      task_pool_.destroy(p);
      return nullptr;
    }
    return static_cast<IActive*>(p);
  }

  task_pool task_pool_;
  regular_task_list regular_task_list_;
  delayd_task_list delayd_task_list_;
};
}  // namespace ae

#endif  // AETHER_TASKS_DETAILS_TASK_MANAGER_H_
