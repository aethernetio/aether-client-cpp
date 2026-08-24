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

#ifndef AETHER_ACTIONS_ACTION_POLL_H_
#define AETHER_ACTIONS_ACTION_POLL_H_

#include <variant>
#include <type_traits>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <etl/pool.h>
#include <etl/vector.h>
#include <etl/variant_pool.h>
DISABLE_WARNING_POP()

#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

namespace ae {
// ActionPool destroys finished actions asynchronously via the scheduler so the
// Finish callback may return before the object is destroyed.
//
// Destroy requests are coalesced into a fixed-capacity pending list and a
// single drain task. A single TaskSubscription must not be overwritten per
// finished action — that drops prior destroy work and leaks pool slots
// (pool_no_allocation under load).
template <ActionContext AC, typename T, std::size_t Capacity>
class ActionPool : public etl::pool<T, Capacity> {
 public:
  static_assert(std::is_base_of_v<Action, T>, "T must be a subclass of Action");

  using base_t = etl::pool<T, Capacity>;

  constexpr explicit ActionPool(AC const& ac) noexcept : ac_{ac} {}
  ~ActionPool() noexcept {
    // Cancel any scheduled drain; remaining live elements are destroyed below.
    drain_ts_ = {};
    drain_scheduled_ = false;
    pending_destroy_.clear();
    for (auto i = base_t::begin(); i != base_t::end(); ++i) {
      base_t::destroy(&i.template get<T>());
    }
  }

  template <typename... Args>
  T* Create(Args&&... args) {
    auto* p = base_t::create(std::forward<Args>(args)...);
    if (p != nullptr) {
      static_cast<Action*>(p)->finished_event().Subscribe(
          [this, p]() { DestroyRequested(p); });
    }
    return p;
  }

  // Test / diagnostics helpers.
  std::size_t pending_destroy_count() const noexcept {
    return pending_destroy_.size();
  }
  bool drain_scheduled() const noexcept { return drain_scheduled_; }

 private:
  void DestroyRequested(T* p) {
    for (auto* existing : pending_destroy_) {
      if (existing == p) {
        return;
      }
    }
    pending_destroy_.push_back(p);
    ScheduleDrain();
  }

  void ScheduleDrain() {
    if (drain_scheduled_) {
      return;
    }
    drain_scheduled_ = true;
    drain_ts_ = ac_.scheduler().Task([this]() { Drain(); });
  }

  void Drain() {
    drain_scheduled_ = false;
    etl::vector<T*, Capacity> batch;
    for (auto* p : pending_destroy_) {
      batch.push_back(p);
    }
    pending_destroy_.clear();
    for (auto* p : batch) {
      if (p != nullptr) {
        base_t::template destroy<T>(p);
      }
    }
    if (!pending_destroy_.empty()) {
      ScheduleDrain();
    }
  }

  AC ac_;
  TaskSubscription drain_ts_;
  bool drain_scheduled_{false};
  etl::vector<T*, Capacity> pending_destroy_;
};

template <ActionContext AC, typename... T, std::size_t Capacity>
class ActionPool<AC, std::variant<T...>, Capacity>
    : public etl::variant_pool<Capacity, T...> {
 public:
  static_assert((std::is_base_of_v<Action, T> && ...),
                "T must be a subclass of Action");
  using base_t = etl::variant_pool<Capacity, T...>;

  constexpr explicit ActionPool(AC const& ac) noexcept : ac_{ac} {}
  ~ActionPool() noexcept {
    drain_ts_ = {};
    drain_scheduled_ = false;
    pending_destroy_.clear();
    for (auto i = base_t::begin(); i != base_t::end(); ++i) {
      base_t::destroy(&i.template get<Action>());
    }
  }

  template <typename U, typename... Args>
    requires(std::is_same_v<U, T> || ...)
  U* Create(Args&&... args) {
    auto* p = base_t::template create<U, Args...>(std::forward<Args>(args)...);
    if (p != nullptr) {
      static_cast<Action*>(p)->finished_event().Subscribe(
          [this, p_ = static_cast<Action*>(p)]() { DestroyRequested(p_); });
    }
    return p;
  }

  std::size_t pending_destroy_count() const noexcept {
    return pending_destroy_.size();
  }
  bool drain_scheduled() const noexcept { return drain_scheduled_; }

 private:
  void DestroyRequested(Action* p) {
    for (auto* existing : pending_destroy_) {
      if (existing == p) {
        return;
      }
    }
    pending_destroy_.push_back(p);
    ScheduleDrain();
  }

  void ScheduleDrain() {
    if (drain_scheduled_) {
      return;
    }
    drain_scheduled_ = true;
    drain_ts_ = ac_.scheduler().Task([this]() { Drain(); });
  }

  void Drain() {
    drain_scheduled_ = false;
    etl::vector<Action*, Capacity> batch;
    for (auto* p : pending_destroy_) {
      batch.push_back(p);
    }
    pending_destroy_.clear();
    for (auto* p : batch) {
      if (p != nullptr) {
        base_t::destroy(p);
      }
    }
    if (!pending_destroy_.empty()) {
      ScheduleDrain();
    }
  }

  AC ac_;
  TaskSubscription drain_ts_;
  bool drain_scheduled_{false};
  etl::vector<Action*, Capacity> pending_destroy_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_POLL_H_
