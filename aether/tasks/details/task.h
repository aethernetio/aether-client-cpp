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

#ifndef AETHER_TASKS_DETAILS_TASK_H_
#define AETHER_TASKS_DETAILS_TASK_H_

#include <cstdint>

namespace ae {
class ITaskSubscription {
 public:
  virtual void Unlink() noexcept = 0;

 protected:
  ~ITaskSubscription() = default;
};

class IActive {
 public:
  static constexpr std::uintptr_t kMagic = 0xda;

  virtual ~IActive() noexcept {
    if ((active != kMagic) && (active != 0)) {
      reinterpret_cast<ITaskSubscription*>(active)  // NOLINT(*no-int-to-ptr)
          ->Unlink();
    }
  }

  std::uintptr_t active{kMagic};
};
/**
 * \brief A general task what can only invoke.
 */
class ITask : public IActive {
 public:
  ~ITask() override = default;

  virtual void Invoke() && noexcept = 0;
};

/**
 * \brief A delayed task invocable only once.
 */
template <typename TimePoint>
class IDelayedTask : public ITask {
 public:
  explicit IDelayedTask(TimePoint exp_at) noexcept : expire_at{exp_at} {}
  ~IDelayedTask() override = default;

  TimePoint expire_at;
};

}  // namespace ae
#endif  // AETHER_TASKS_DETAILS_TASK_H_
