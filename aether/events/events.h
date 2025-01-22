/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_EVENTS_EVENTS_H_
#define AETHER_EVENTS_EVENTS_H_

#include <vector>
#include <memory>
#include <utility>
#include <cassert>
#include <algorithm>
#include <type_traits>

#include "aether/events/event_handler.h"
#include "aether/events/event_subscription.h"

namespace ae {
template <typename TSignature>
class EventSubscriber;

template <typename TSignature>
class Event;

/**
 * \brief Storage for subscriptions to some events
 */
template <typename... TArgs>
class Event<void(TArgs...)> {
  class EventEmitter {
   public:
    ~EventEmitter() {
      for (auto& subscription : subscriptions_) {
        subscription.Reset();
      }
    }

    // store self to prevent removing while emit
    void Emit(std::shared_ptr<EventEmitter> self_ptr, TArgs... args) {
      // TODO: find a better way to invoke all handlers
      /*
       * invoke_list is using to prevent iterator invalidation while new
       * handlers added during invocation and recursive event emit.
       * Clean up after invocation used for the same reason.
       */

      // add new subscriptions
      auto invoke_list = subscriptions_;
      for (auto& subscription : invoke_list) {
        // invoke subscription handler
        subscription.invoke(std::forward<TArgs>(args)...);
      }
      // clean up dead subscriptions
      subscriptions_.erase(
          std::remove_if(std::begin(subscriptions_), std::end(subscriptions_),
                         [](auto const& subscription) {
                           return !subscription.is_alive();
                         }),
          std::end(subscriptions_));

      // just to be used
      self_ptr.reset();
    }

    void Add(EventHandlerSubscription&& handler) {
      subscriptions_.emplace_back(std::move(handler));
    }

   private:
    std::vector<EventHandlerSubscription> subscriptions_;
  };

 public:
  using CallbackSignature = void(TArgs...);
  using Subscriber = EventSubscriber<CallbackSignature>;

  template <typename TCallback>
  static constexpr bool kIsInvocable =
      std::is_invocable_r_v<void, std::decay_t<TCallback>, TArgs...>;

  Event() : emitter_{std::make_shared<EventEmitter>()} {}
  ~Event() = default;

  Event(Event const& other) = delete;
  Event(Event&& other) noexcept = default;

  Event& operator=(Event const& other) = delete;
  Event& operator=(Event&& other) noexcept = default;

  /**
   * \brief Invoke all handlers to this event.
   * Some handlers may call this recursively and either unsubscribe or make new
   * subscriptions.
   */
  void Emit(TArgs... args) {
    emitter_->Emit(emitter_, std::forward<TArgs>(args)...);
  }

  /**
   * \brief Add new subscription to this event
   * Users should use EventSubscriber.
   */
  void Add(EventHandlerSubscription&& handler) {
    emitter_->Add(std::move(handler));
  }

 private:
  std::shared_ptr<EventEmitter> emitter_;
};

/**
 * \brief Helper class to make event subscriptions.
 * It is designed to be constructed implicitly from Event<T> and to be returned
 * from getter to Event<T> in classes
 */
template <typename TSignature>
class EventSubscriber {
 public:
  EventSubscriber(Event<TSignature>& event) : event_{&event} {}

  /**
   * \brief Create new subscription to event with callback to Event's signature.
   *
   * \return Subscription object - store it somewhere while you required
   * subscription to be active \see Subscription
   */
  template <typename TCallback>
  [[nodiscard]] auto Subscribe(TCallback&& cb) {
    static_assert(Event<TSignature>::template kIsInvocable<TCallback>,
                  "TCallable must have same signature");

    std::shared_ptr<IEventHandler> event_handler =
        std::make_unique<EventHandler<TSignature>>(std::forward<TCallback>(cb));

    event_->Add(EventHandlerSubscription{event_handler});
    return Subscription(std::move(event_handler));
  }

 private:
  Event<TSignature>* event_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_H_
