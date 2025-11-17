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
#include "aether/events/event_list.h"
#include "aether/events/event_handler.h"
#include "aether/events/event_deleter.h"
#include "aether/types/small_function.h"
#include "aether/events/event_subscription.h"

namespace ae {
template <typename TSignature>
class Event;

template <typename TSignature>
class EventSubscriber;

/**
 * \brief Storage for handlers to some event
 */
template <typename... TArgs>
class Event<void(TArgs...)> {
 public:
  using CallbackSignature = void(TArgs...);
  using Subscriber = EventSubscriber<CallbackSignature>;
  using List = EventHandlersList;

  explicit Event() : events_list_{MakeRcPtr<List>()} {}

  ~Event() = default;

  AE_CLASS_MOVE_ONLY(Event)

  /**
   * \brief Invoke all handlers to this event.
   * Some handlers may call this recursively and either unsubscribe or make new
   * subscriptions.
   */
  void Emit(TArgs... args) {
    EmitImpl(events_list_, std::forward<TArgs>(args)...);
  }

  /**
   * \brief Add new subscription to this event
   * Users should use EventSubscriber.
   * returns event deleter
   */
  auto Add(EventHandler<CallbackSignature>&& handler) {
    auto index = events_list_->Insert(std::move(handler));
    return EventHandlerDeleter{events_list_, index};
  }

 private:
  /**
   * \brief Invoke handler to this event.
   * Separate static emitter is used to handle self destruction while handler
   * invoking.
   */
  static void EmitImpl(RcPtr<List> events_list, TArgs&&... args) {
    auto iterate_list = events_list->Iterator();
    for (auto it = std::begin(iterate_list); it != std::end(iterate_list);
         ++it) {
      auto* handler = it.get<CallbackSignature>();
      assert((handler != nullptr) && "Handler is null");
      handler->Invoke(std::forward<TArgs>(args)...);
    }
  }

  RcPtr<List> events_list_;
};

/**
 * \brief Helper class to make event subscriptions.
 * It is designed to be constructed implicitly from Event<T> and to be returned
 * from getter to Event<T> in classes
 */
template <typename... TArgs>
class EventSubscriber<void(TArgs...)> {
 public:
  using Signature = void(TArgs...);
  using Subscription = ae::Subscription;
  using EventType = Event<Signature>;

  template <typename TCallback>
  static constexpr bool kIsInvocable =
      std::is_invocable_r_v<void, std::decay_t<TCallback>, TArgs...>;

  EventSubscriber(EventType& event) : event_{&event} {}

  /**
   * \brief Create new subscription to event with callback to Event's signature.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  template <typename TCallback>
  auto Subscribe(TCallback&& cb) {
    static_assert(std::is_invocable_v<std::decay_t<TCallback>, TArgs...>,
                  "TCallable must have same signature");
    return event_->Add(EventHandler<Signature>{std::forward<TCallback>(cb)});
  }

  /**
   * \brief Create new subscription to event with pointer to member function.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  template <auto Method>
  auto Subscribe(MethodPtr<Method> method) {
    static_assert(std::is_invocable_v<decltype(Method),
                                      decltype(method.instance), TArgs...>,
                  "Method must have same signature");
    return event_->Add(EventHandler<Signature>{method});
  }

  /**
   * \brief Create new subscription to event with pointer to free function.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  auto Subscribe(void (*method)(TArgs...)) {
    static_assert(std::is_invocable_v<decltype(method), TArgs...>,
                  "method must have same signature");
    return event_->Add(EventHandler<Signature>{method});
  }

  /**
   * \brief Create new subscription to event with different event with same
   * signature.
   *
   * \return EventHandlerDeleter to remove handler after it does not needed
   * anymore \see Subscription for RAII wrapper
   */
  auto Subscribe(Event<void(TArgs...)>& event) {
    return event_->Add(EventHandler<Signature>{
        MethodPtr<&Event<void(TArgs...)>::Emit>{&event}});
  }

 private:
  EventType* event_;
};

template <typename... TArgs>
EventSubscriber(Event<void(TArgs...)>&) -> EventSubscriber<void(TArgs...)>;

}  // namespace ae

#endif  // AETHER_EVENTS_EVENTS_H_
