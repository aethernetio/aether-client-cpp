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

#ifndef AETHER_EVENTS_DETAILS_EVENT_OBJECT_H_
#define AETHER_EVENTS_DETAILS_EVENT_OBJECT_H_

#include <cassert>
#include <concepts>
#include <cstdlib>
#include <type_traits>
#include <utility>

#include "aether/events/details/event_registered_handler.h"
#include "aether/events/details/event_system.h"
#include "aether/events/details/generic_event_handler.h"

namespace ae::events {
template <typename T>
concept EventContext = requires(T const& t) {
  { t.event_system() } -> EventSystemConcept;
};

/**
 * \brief The event object stored in class.
 * The event emitted using this object.
 * The subscription added also to this object.
 * Event object must be initialized with suitable event context or reference to
 * event system. The event system must outlive this object and all of its
 * subscriptions. Construction and subscription fail fast if their fixed
 * capacities are exhausted. Multicast payloads passed by value must be
 * copyable because every handler receives a separate copy. Reference payloads
 * are shared synchronously by all handlers; handlers must not retain them.
 * Move-only by-value payloads and rvalue-reference payloads are unsupported.
 */
template <EventSystemConcept Es, typename Signature>
class EventObject;

template <EventSystemConcept Es, typename... Args>
class EventObject<Es, void(Args...)> {
 public:
  static_assert((!std::is_rvalue_reference_v<Args> && ...),
                "Multicast events do not support T&& parameters");
  static_assert(((std::is_reference_v<Args> ||
                  std::copy_constructible<std::remove_cvref_t<Args>>) &&
                 ...),
                "By-value multicast event parameters must be copyable");

  template <EventContext EC>
  explicit EventObject(EC const& ec) : EventObject{ec.event_system()} {}

  explicit EventObject(Es& es) : es_{&es} {
    auto k = es_->RegEvent();
    if (!k) {
      assert(false && "Event should be registered on construction");
      std::abort();
    }
    key_ = *k;
  }

  ~EventObject() {
    if (es_ != nullptr) {
      es_->UnregEvent(key_);
    }
  }

  // no copy, but move
  EventObject(EventObject const&) = delete;
  EventObject& operator=(EventObject const&) = delete;

  // Moving transfers the event registration. The source becomes invalid and
  // must not be used except for destruction, assignment, or a validity check.
  EventObject(EventObject&& other) noexcept : es_{other.es_}, key_{other.key_} {
    other.es_ = nullptr;
  }
  EventObject& operator=(EventObject&& other) noexcept {
    if (this != &other) {
      if (es_ != nullptr) {
        es_->UnregEvent(key_);
      }
      es_ = other.es_;
      key_ = other.key_;
      other.es_ = nullptr;
    }
    return *this;
  }

  /**
   * \brief Emit this event synchronously with the supplied arguments.
   *
   * Dispatch uses an event-system snapshot and does not hold its mutex while
   * calling handlers. Calling this on a moved-from EventObject violates its
   * contract.
   */
  void Emit(Args... args) {
    assert(es_ != nullptr && "EventObject is invalid");
    es_->template Invoke<Args...>(key_, args...);
  }

  /**
   * \brief Subscribe a handler and return its non-owning registration token.
   *
   * The returned RegHandler does not own the subscription. Store it in a
   * Subscription or MultiSubscription when the handler must be removed.
   * Calling this on a moved-from EventObject violates its contract.
   */
  template <std::derived_from<Handler<void(Args...)>> T>
  RegHandler<Es> Subscribe(T&& handler) const {
    assert(es_ != nullptr && "EventObject is invalid");
    auto* h = es_->AddHandler(key_, std::forward<T>(handler));
    if (h == nullptr) {
      std::abort();
    }
    return RegHandler{
        .key = key_,
        .h = h,
        .es = es_,
    };
  }

  /**
   * \brief Make subscription using generic handler from any invocable functor
   */
  template <std::invocable<Args...> Func>
  RegHandler<Es> Subscribe(Func&& func) const {
    return Subscribe(
        GenericHandler<void(Args...), Func>{std::forward<Func>(func)});
  }

  explicit operator bool() const { return es_ != nullptr; }

 private:
  Es* es_{nullptr};
  Key key_{};
};

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_OBJECT_H_
