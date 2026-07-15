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

#ifndef AETHER_TASKS_DETAILS_MANUAL_TASK_SCHEDULER_H_
#define AETHER_TASKS_DETAILS_MANUAL_TASK_SCHEDULER_H_

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>  // // IWYU pragma: keep
#include <mutex>

#include "aether/tasks/details/task_manager.h"

namespace ae {
template <typename TaskManagerConf,
          typename TimePointType = std::chrono::system_clock::time_point>
class ManualTaskScheduler {
  using task_manager = TaskManager<TaskManagerConf>;
  using regular_list_t =
      std::decay_t<decltype(std::declval<task_manager>().regular())>::list;
  using delayed_list_t =
      std::decay_t<decltype(std::declval<task_manager>().delayed())>::list;

 public:
  ManualTaskScheduler() = default;

  template <typename F>
  auto Task(F&& f) {
    return AddSafe([&]() { return task_manager_.Task(std::forward<F>(f)); });
  }

  template <typename F, typename TP>
  auto DelayedTask(F&& f, TP tp) {
    return AddSafe(
        [&]() { return task_manager_.DelayedTask(std::forward<F>(f), tp); });
  }

  /**
   * \brief Call Update on a loop with current time.
   * \param current_time - current time point is used to check if delayed tasks
   * expired
   * \return the recommended time point for next update call.
   */
  TimePointType Update(
      TimePointType current_time = TimePointType::clock::now()) {
    auto lock = std::unique_lock{lock_};
    trigger_.store(false, std::memory_order::release);
    CheckOverflows();

    // run regular tasks
    task_manager_.regular().StealTasks(reg_list_);
    UpdateTasks(lock, reg_list_, task_manager_.regular());

    // run delaed tasks
    task_manager_.delayed().StealTasks(current_time, delay_list_);
    UpdateTasks(lock, delay_list_, task_manager_.delayed());

    // return amount of time for next update
    if (task_manager_.delayed().size() != 0) {
      return task_manager_.delayed().back()->expire_at;
    }
    return TimePointType::max();
  }

  /**
   * \brief Wait until wake up time or while the new task is added.
   * The Update method should be called after WaitUntil returns.
   * \param wake_up_time - maximum time to wait.
   */
  void WaitUntil(TimePointType wake_up_time) {
    // fast check without mutex locking
    if (trigger_.load(std::memory_order::acquire)) {
      return;
    }

    auto lock = std::unique_lock{lock_};
    cv_.wait_until(lock, wake_up_time, [this]() noexcept {
      return trigger_.load(std::memory_order::acquire);
    });
  }

  std::size_t overflow_counter() const { return overflow_counter_; }

 private:
  template <typename F>
  IActive* AddSafe(F&& f) {
    auto lock = std::scoped_lock{lock_};
    auto* p = std::invoke(std::forward<F>(f));
    if (p == nullptr) {
      overflow_counter_++;
    }
    trigger_.store(true, std::memory_order::release);
    cv_.notify_one();
    return p;
  }

  template <typename List, typename StealList>
  void UpdateTasks(std::unique_lock<std::mutex>& lock, StealList& tasks,
                   List& list) {
    lock.unlock();
    for (auto* t : tasks) {
      if (t->active != 0) {
        // each task invoked only once
        std::move(*t).Invoke();
      }
    }
    lock.lock();
    list.Free(tasks);
    tasks.clear();
  }

  void CheckOverflows() {
#ifndef NDEBUG
    if (overflow_counter_ != 0) {
      fprintf(stderr, "!!!!!> ManualTaskScheduler: got overflow: %zu\n",
              overflow_counter_);
      overflow_counter_ = 0;
    }
#endif
  }

  task_manager task_manager_;
  regular_list_t reg_list_;
  delayed_list_t delay_list_;

  std::mutex lock_;
  std::atomic_bool trigger_{false};
  std::condition_variable cv_;
  std::size_t overflow_counter_{};
};
}  // namespace ae

#endif  // AETHER_TASKS_DETAILS_MANUAL_TASK_SCHEDULER_H_
