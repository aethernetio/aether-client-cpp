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

#ifndef AETHER_TASKS_DETAILS_TASK_QUEUE_H_
#define AETHER_TASKS_DETAILS_TASK_QUEUE_H_

#include <cstddef>
#include <iterator>
#include <algorithm>

#include "aether/tasks/details/task.h"

#include "aether/warning_disable.h"
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/etl/include/etl/vector.h"
DISABLE_WARNING_POP()

namespace ae {
template <typename Interface, std::size_t Capacity, typename Pool>
class TaskQueueBase {
 public:
  static constexpr std::size_t kCapacity = Capacity;
  template <std::size_t max_size>
  using list_container = etl::vector<Interface*, max_size>;
  using list = list_container<kCapacity>;

  explicit TaskQueueBase(Pool& pool) : pool_{&pool} {}
  ~TaskQueueBase() { Free(list_); }

  /**
   * \brief Free elements.
   */
  template <std::size_t max_size>
  void Free(list_container<max_size>& elements) {
    for (auto* e : elements) {
      pool_->template destroy<Interface>(e);
    }
  }

  std::size_t size() const { return list_.size(); }
  Interface* back() const { return list_.back(); }
  Interface* front() const { return list_.front(); }

 protected:
  Pool* pool_;
  list list_;
};

template <std::size_t Capacity, typename Pool>
class TaskQueue : TaskQueueBase<ITask, Capacity, Pool> {
 public:
  using base = TaskQueueBase<ITask, Capacity, Pool>;
  static constexpr std::size_t kCapacity = base::kCapacity;
  template <std::size_t max_size>
  using list_container = typename base::template list_container<max_size>;
  using list = typename base::list;

  using base::base;

  using base::back;
  using base::front;
  using base::size;

  using base::Free;

  bool Add(ITask* p) {
    if (base::list_.size() == base::list_.max_size()) {
      return false;
    }
    base::list_.emplace_back(p);
    return true;
  }

  /**
   * \brief Steal elements.
   * Stealled elements should be freed with Free function \see Free
   */
  template <std::size_t max_count>
  void StealTasks(list_container<max_count>& to) {
    auto start = std::begin(base::list_);
    auto end = (base::list_.size() > max_count) ? start + max_count
                                                : std::end(base::list_);
    to.insert(to.end(), start, end);
    base::list_.erase(start, end);
  }
};

template <std::size_t Capacity, typename Pool>
class DelayedTaskQueue : TaskQueueBase<IDelayedTask, Capacity, Pool> {
 public:
  using base = TaskQueueBase<IDelayedTask, Capacity, Pool>;
  static constexpr std::size_t kCapacity = base::kCapacity;
  template <std::size_t max_size>
  using list_container = typename base::template list_container<max_size>;
  using list = typename base::list;

  using base::base;

  using base::back;
  using base::front;
  using base::size;

  using base::Free;

  bool Add(IDelayedTask* p) {
    if (base::list_.size() == base::list_.max_size()) {
      return false;
    }
    // keep list sorted by expire_at
    auto it = std::find_if(
        std::rbegin(base::list_), std::rend(base::list_),
        [&](IDelayedTask const* e) { return p->expire_at < e->expire_at; });

    base::list_.emplace(it.base(), p);
    return true;
  }

  /**
   * \brief Steal elements expired before expiratrion_time.
   * Stealled elements should be freed with Free function \see Free
   */
  template <std::size_t max_count>
  void StealTasks(TimePoint expiration_time, list_container<max_count>& to) {
    auto it = std::rbegin(base::list_);
    std::size_t count = 0;
    for (; it != std::rend(base::list_); ++it) {
      if (((*it)->expire_at <= expiration_time) && (++count <= max_count)) {
        continue;
      }
      break;
    }
    if (count == 0) {
      return;
    }

    auto start = it.base();
    to.insert(to.end(), start, std::end(base::list_));
    base::list_.erase(start, std::end(base::list_));
  }
};
}  // namespace ae

#endif  // AETHER_TASKS_DETAILS_TASK_QUEUE_H_
