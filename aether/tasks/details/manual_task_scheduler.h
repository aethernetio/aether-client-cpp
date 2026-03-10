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

#include <mutex>
#include <atomic>
#include <cassert>
#include <iostream>
#include <condition_variable>

#include "aether/clock.h"

#include "aether/tasks/details/task_manager.h"

namespace ae {
template <typename TaskManagerConf>
class ManualTaskScheduler {
  using task_manager = TaskManager<TaskManagerConf>;
  using regular_list_t = typename std::decay_t<
      decltype(std::declval<task_manager>().regular())>::list;
  using delayed_list_t = typename std::decay_t<
      decltype(std::declval<task_manager>().delayed())>::list;

 public:
  ManualTaskScheduler() = default;

  template <typename F>
  void Task(F&& f) {
    AddSafe([&]() { return task_manager_.Task(std::forward<F>(f)); });
  }

  template <typename F>
  void DelayedTask(F&& f, Duration dur) {
    AddSafe(
        [&]() { return task_manager_.DelayedTask(std::forward<F>(f), dur); });
  }
  template <typename F>
  void DelayedTask(F&& f, TimePoint tp) {
    AddSafe(
        [&]() { return task_manager_.DelayedTask(std::forward<F>(f), tp); });
  }

  /**
   * \brief Call Update on a loop with current time.
   * \param current_time - current time point is used to check if delayed tasks
   * expired
   * \return the recommended time point for next update call.
   */
  TimePoint Update(TimePoint current_time = Now()) {
    auto lock = std::unique_lock{lock_};
    trigger_ = false;
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
    return TimePoint::max();
  }

  /**
   * \brief Wait until wake up time or while the new task is added.
   * The Update method should be called after WaitUntil returns.
   * \param wake_up_time - maximum time to wait.
   */
  void WaitUntil(TimePoint wake_up_time) {
    auto lock = std::unique_lock{lock_};
    if (trigger_.exchange(false)) {
      return;
    }
    cv_.wait_until(lock, wake_up_time, [this]() { return trigger_.load(); });
    trigger_ = false;
  }

  std::size_t overflow_counter() const { return overflow_counter_; }

 private:
  template <typename F>
  void AddSafe(F&& f) {
    auto lock = std::scoped_lock{lock_};
    if (!std::invoke(std::forward<F>(f))) {
      overflow_counter_++;
    }
    trigger_ = true;
    cv_.notify_one();
  }

  template <typename List, typename StealList>
  void UpdateTasks(std::unique_lock<std::mutex>& lock, StealList& tasks,
                   List& list) {
    lock.unlock();
    for (auto* t : tasks) {
      t->Invoke();
    }
    lock.lock();
    list.Free(tasks);
    tasks.clear();
  }

  void CheckOverflows() {
#ifndef NDEBUG
    if (overflow_counter_ != 0) {
      std::cerr << "ManualTaskScheduler: got overflow: " << overflow_counter_
                << '\n';
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
