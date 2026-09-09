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

#ifndef AETHER_EVENTS_DETAILS_EVENT_MUILTI_SUBSCRIPTION_H_
#define AETHER_EVENTS_DETAILS_EVENT_MUILTI_SUBSCRIPTION_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <vector>

#include <etl/vector.h>

#include "aether/events/details/event_registered_handler.h"
#include "aether/events/details/event_system.h"

namespace ae::events {
/**
 * \brief A RAII object to handle multiple event subscriptions at the same time.
 *
 * Reset() and destruction remove all registrations. The event system and the
 * events that produced the registrations must outlive this object. The dynamic
 * public specialization uses std::vector and can allocate while growing; the
 * fixed specialization preflights its capacity and fails fast on overflow.
 * Push() requires valid registrations from one event system, each transferred
 * to this as its sole owner; using copied RegHandler tokens in multiple owners
 * is unsupported misuse. Moving transfers that ownership and leaves the source
 * empty.
 */
template <EventSystemConcept Es, template <typename> typename Vector>
class MultiSubscriptionObject {
  struct ReducedRegHandler {
    Key key;
    IHandler* h;
  };

 public:
  MultiSubscriptionObject() = default;
  MultiSubscriptionObject(MultiSubscriptionObject const&) = delete;
  MultiSubscriptionObject(MultiSubscriptionObject&& other) noexcept
      : es_{other.es_}, reg_handlers_{std::move(other.reg_handlers_)} {
    other.es_ = nullptr;
    other.reg_handlers_.clear();
  }

  MultiSubscriptionObject& operator=(MultiSubscriptionObject const&) = delete;
  MultiSubscriptionObject& operator=(MultiSubscriptionObject&& other) noexcept {
    if (this != &other) {
      Reset();
      es_ = other.es_;
      reg_handlers_ = std::move(other.reg_handlers_);
      other.es_ = nullptr;
      other.reg_handlers_.clear();
    }
    return *this;
  }

  ~MultiSubscriptionObject() { Reset(); }

  template <typename... REs>
    requires(std::is_same_v<REs, RegHandler<Es>> && ...)
  void Push(RegHandler<Es> const& re, REs const&... res) {
    assert(re.es != nullptr && "Registration event system is null");
    assert(re.h != nullptr && "Registration handler is null");
    assert(((res.es != nullptr && res.h != nullptr) && ...) &&
           "Registration is invalid");
    CleanUp();
    SetEventSystem(re, res...);
    auto const count = 1 + sizeof...(res);
    PreflightCapacity(count);
    reg_handlers_.reserve(reg_handlers_.size() + count);
    reg_handlers_.emplace_back(ReducedRegHandler{.key = re.key, .h = re.h});
    // push the rest
    (reg_handlers_.emplace_back(ReducedRegHandler{.key = res.key, .h = res.h}),
     ...);
  }

  MultiSubscriptionObject& operator+=(RegHandler<Es> const& re) {
    Push(re);
    return *this;
  }

  void Reset() {
    if (reg_handlers_.empty()) {
      return;
    }
    assert(es_ != nullptr && "Registration event system is null");
    auto to_reset = std::move(reg_handlers_);
    for (auto const& rrh : to_reset) {
      assert(rrh.h != nullptr && "Registration handler is null");
      es_->RemoveHandler(rrh.key, rrh.h);
    }
    reg_handlers_.clear();
  }

  explicit operator bool() const { return !reg_handlers_.empty(); }

 private:
  template <typename... REs>
  void SetEventSystem(RegHandler<Es> const& re, REs const&... res) {
    if (es_ == nullptr) {
      es_ = re.es;
    }
    assert(es_ == re.es && "Registration event systems differ");
    assert(((es_ == res.es) && ...) && "Registration event systems differ");
  }

  void PreflightCapacity(std::size_t count) const {
    if (count > reg_handlers_.max_size() - reg_handlers_.size()) {
      std::abort();
    }
  }

  void CleanUp() {
    if (reg_handlers_.empty()) {
      return;
    }
    assert(es_ != nullptr && "Registration event system is null");
    // remove all handler for events, that already unregistered
    reg_handlers_.erase(
        std::remove_if(std::begin(reg_handlers_), std::end(reg_handlers_),
                       [this](auto const& rrh) noexcept {
                         return !es_->IsRegistered(rrh.key);
                       }),
        std::end(reg_handlers_));
  };

  Es* es_{nullptr};
  Vector<ReducedRegHandler> reg_handlers_;
};

/**
 * \brief Multi-subscription with dynamic std::vector storage.
 *
 * This compatibility specialization may allocate when registrations are added.
 */
template <EventSystemConcept Es>
using MultiSubscriptionObjectDyn = MultiSubscriptionObject<Es, std::vector>;

namespace events_multi_subscription_internal {
template <std::size_t Capacity>
struct EtlVector {
  template <typename V>
  using type = etl::vector<V, Capacity>;
};
}  // namespace events_multi_subscription_internal

/**
 * \brief Multi-subscription with fixed etl::vector storage.
 *
 * Push() fails fast before changing storage when the requested registrations do
 * not fit within Capacity.
 */
template <EventSystemConcept Es, std::size_t Capacity>
using MultiSubscriptionObjectFix = MultiSubscriptionObject<
    Es, events_multi_subscription_internal::EtlVector<Capacity>::template type>;

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_MUILTI_SUBSCRIPTION_H_
