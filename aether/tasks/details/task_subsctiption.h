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

#ifndef AETHER_TASKS_DETAILS_TASK_SUBSCRIPTION_H_
#define AETHER_TASKS_DETAILS_TASK_SUBSCRIPTION_H_

#include <utility>

#include "aether/tasks/details/task.h"

namespace ae {
class TaskSubscription final : public ITaskSubscription {
 public:
  constexpr TaskSubscription() noexcept = default;
  TaskSubscription(IActive* p)  // NOLINT(*explicit-constructor)
      noexcept
      : ptr_{p} {
    if (ptr_ != nullptr) {
      ptr_->active = reinterpret_cast<std::uintptr_t>(this);
    }
  }

  ~TaskSubscription() noexcept { Reset(); }

  TaskSubscription(TaskSubscription const&) = delete;
  TaskSubscription& operator=(TaskSubscription const&) = delete;

  TaskSubscription(TaskSubscription&& other) noexcept : ptr_{other.ptr_} {
    other.ptr_ = nullptr;
    if (ptr_ != nullptr) {
      ptr_->active = reinterpret_cast<std::uintptr_t>(this);
    }
  }

  TaskSubscription& operator=(TaskSubscription&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(ptr_, other.ptr_);
      if (ptr_ != nullptr) {
        ptr_->active = reinterpret_cast<std::uintptr_t>(this);
      }
    }
    return *this;
  }

  TaskSubscription& operator=(IActive* p) noexcept {
    Reset();
    ptr_ = p;
    ptr_->active = reinterpret_cast<std::uintptr_t>(this);
    return *this;
  }

  constexpr void Reset() noexcept {
    if (ptr_ != nullptr) {
      ptr_->active = 0;
    }
    ptr_ = nullptr;
  }

  void Unlink() noexcept override { ptr_ = nullptr; }

  explicit constexpr operator bool() const noexcept {
    return (ptr_ != nullptr) && (ptr_->active != 0);
  }

 private:
  IActive* ptr_{nullptr};
};
}  // namespace ae

#endif  // AETHER_TASKS_DETAILS_TASK_SUBSCRIPTION_H_
