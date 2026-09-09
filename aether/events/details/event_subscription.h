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

#ifndef AETHER_EVENTS_DETAILS_EVENT_SUBSCRIPTION_H_
#define AETHER_EVENTS_DETAILS_EVENT_SUBSCRIPTION_H_

#include <cassert>
#include <optional>
#include <utility>

#include "aether/events/details/event_registered_handler.h"
#include "aether/events/details/event_system.h"

namespace ae::events {
/**
 * \brief A RAII object to handle event subscription.
 * Removes the event handler on Reset() or destruction.
 *
 * The event system and event that produced the registration must outlive this
 * object. Construction and assignment require a valid RegHandler produced by
 * EventObject::Subscribe and transferred to this as its sole owner. Although
 * RegHandler is copyable, using copies to give a registration multiple owners
 * is unsupported misuse. Reset() does not wait for a callback already
 * selected for dispatch.
 */
template <EventSystemConcept Es>
class SubscriptionObject {
 public:
  SubscriptionObject() = default;
  SubscriptionObject(RegHandler<Es> const& re)  // NOLINT(*explicit*)
      : reg_handler_{re} {
    assert(re.es != nullptr && "Registration event system is null");
    assert(re.h != nullptr && "Registration handler is null");
  }

  SubscriptionObject& operator=(RegHandler<Es> const& re) {
    assert(re.es != nullptr && "Registration event system is null");
    assert(re.h != nullptr && "Registration handler is null");
    Reset();
    reg_handler_.emplace(re);
    return *this;
  }

  SubscriptionObject(SubscriptionObject const& other) = delete;
  SubscriptionObject(SubscriptionObject&& other) noexcept
      : reg_handler_{std::move(other.reg_handler_)} {
    other.reg_handler_.reset();
  }

  SubscriptionObject& operator=(SubscriptionObject const& other) = delete;
  SubscriptionObject& operator=(SubscriptionObject&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(reg_handler_, other.reg_handler_);
    }
    return *this;
  }

  ~SubscriptionObject() { Reset(); }

  void Reset() {
    if (reg_handler_) {
      assert(reg_handler_->es != nullptr &&
             "Registration event system is null");
      assert(reg_handler_->h != nullptr && "Registration handler is null");
      reg_handler_->es->RemoveHandler(reg_handler_->key, reg_handler_->h);
      reg_handler_.reset();
    }
  }

  explicit operator bool() const { return reg_handler_.has_value(); }

 private:
  std::optional<RegHandler<Es>> reg_handler_;
};
}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_SUBSCRIPTION_H_
