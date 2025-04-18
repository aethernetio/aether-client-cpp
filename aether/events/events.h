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

#include <utility>
#include <cassert>
#include <algorithm>
#include <type_traits>

#include "aether/ptr/rc_ptr.h"
#include "aether/events/event_impl.h"
#include "aether/events/event_handler.h"
#include "aether/events/event_deleter.h"
#include "aether/events/event_subscription.h"

namespace ae {
template <typename TSignature, typename TSyncPolicy = NoLockSyncPolicy>
class Event;

template <typename TSignature, typename TSyncPolicy = NoLockSyncPolicy>
class EventSubscriber;

/**
 * \brief Storage for handlers to some event
 */
template <typename... TArgs, typename TSyncPolicy>
class Event<void(TArgs...), TSyncPolicy> {
 public:
  using CallbackSignature = void(TArgs...);
  using Subscriber = EventSubscriber<CallbackSignature, TSyncPolicy>;
  using Impl = EventImpl<CallbackSignature, TSyncPolicy>;

  template <typename TCallback>
  static constexpr bool kIsInvocable =
      std::is_invocable_r_v<void, std::decay_t<TCallback>, TArgs...>;

  explicit Event(TSyncPolicy sync_policy = {})
      : event_impl_{MakeRcPtr<Impl>(std::forward<TSyncPolicy>(sync_policy))} {}

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
    Impl::Emit(event_impl_, std::forward<TArgs>(args)...);
  }

  /**
   * \brief Add new subscription to this event
   * Users should use EventSubscriber.
   * returns event deleter
   */
  auto Add(EventHandler<CallbackSignature>&& handler) {
    auto index = event_impl_->Add(std::move(handler));
    return EventHandlerDeleter{event_impl_, index};
  }

 private:
  RcPtr<Impl> event_impl_;
};

/**
 * \brief Helper class to make event subscriptions.
 * It is designed to be constructed implicitly from Event<T> and to be returned
 * from getter to Event<T> in classes
 */
template <typename TSignature, typename TSyncPolicy>
class EventSubscriber {
 public:
  using EventType = Event<TSignature, TSyncPolicy>;

  EventSubscriber(EventType& event) : event_{&event} {}

  /**
   * \brief Create new subscription to event with callback to Event's signature.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  template <typename TCallback>
  auto Subscribe(TCallback&& cb) {
    static_assert(EventType::template kIsInvocable<TCallback>,
                  "TCallable must have same signature");

    return event_->Add(EventHandler<TSignature>{
        std::function<TSignature>{std::forward<TCallback>(cb)}});
  }

  /**
   * \brief Create new subscription to event with callback to Event's signature.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  template <typename TInstance, auto Method>
  auto Subscribe(TInstance& instance, MethodPtr<Method> const& method) {
    static_assert(
        EventType::template kIsInvocable<
            Delegate<typename FunctionSignature<decltype(Method)>::Signature>>,
        "Method must have the same signature");

    return event_->Add(EventHandler<TSignature>{Delegate{instance, method}});
  }

 private:
  EventType* event_;
};

}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_H_
