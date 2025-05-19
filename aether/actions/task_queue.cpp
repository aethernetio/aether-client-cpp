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

#include "aether/actions/task_queue.h"

#include <utility>

namespace ae {

TimePoint TaskQueue::Update(TimePoint current_time) {
  if (tasks_.empty()) {
    return current_time;
  }
  std::vector<Task> tasks_invoke;
  {
    // steal tasks under the mutex
    auto lock = std::lock_guard{sync_queue_};
    tasks_invoke = std::move(tasks_);
  }
  for (auto& t : tasks_invoke) {
    t();
  }
  return current_time;
}

void TaskQueue::Enqueue(Task&& task) {
  auto lock = std::lock_guard{sync_queue_};
  tasks_.emplace_back(std::move(task));
}

}  // namespace ae
