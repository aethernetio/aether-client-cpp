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

#ifndef AETHER_EVENTS_CUMULATIVE_EVENT_H_
#define AETHER_EVENTS_CUMULATIVE_EVENT_H_

#include <array>
#include <utility>
#include <cassert>
#include <algorithm>
#include <type_traits>

#include "aether/events/events.h"

// TODO: add tests
namespace ae {
/**
 * \brief Accumulates data from other events and emit self after all of
 * them.
 */
template <typename T, std::size_t Count>
class CumulativeEvent {
  template <typename std::size_t I>
  class ValueSetter {
   public:
    explicit ValueSetter(CumulativeEvent* event) : event_(event) {}

    ValueSetter& operator=(T value) {
      event_->values_[I] = value;
      return *this;
    }

   private:
    CumulativeEvent* event_;
  };

 public:
  CumulativeEvent() = default;

  template <typename TFunc, typename... TSignatures>
  explicit CumulativeEvent(
      TFunc&& func, EventSubscriber<TSignatures>&&... event_subscribers) {
    Connect(std::forward<TFunc>(func), std::move(event_subscribers)...);
  }

  template <typename TFunc>
  auto Subscribe(TFunc&& func) {
    return EventSubscriber{res_event_}.Subscribe(std::forward<TFunc>(func));
  }

  template <typename TFunc, typename... EventSubscribers>
  void Connect(TFunc&& func, EventSubscribers&&... event_subscribers) {
    static_assert(sizeof...(EventSubscribers) == Count,
                  "Count of event subscribers should be same as Count");

    ConnectImpl(std::forward<TFunc>(func), std::make_index_sequence<Count>(),
                std::forward<EventSubscribers>(event_subscribers)...);
  }

  T const& operator[](std::size_t i) const {
    assert(i < Count);
    return values_[i];
  }

  [[nodiscard]] auto begin() const { return std::begin(values_); }
  [[nodiscard]] auto end() const { return std::end(values_); }

  constexpr auto size() const { return values_.size(); }

 private:
  template <typename TFunc, typename... EventSubscribers, std::size_t... Is>
  void ConnectImpl(TFunc&& func, std::index_sequence<Is...>,
                   EventSubscribers&&... event_subscribers) {
    ((subscriptions_[Is] =
          std::forward<EventSubscribers>(event_subscribers)
              .Subscribe(
                  [this, func{std::forward<TFunc>(func)}](auto&&... args) {
                    set_map_[Is] = true;
                    ValueSetter<Is> vs{this};
                    func(vs, std::forward<decltype(args)>(args)...);
                    if (IsFull()) {
                      res_event_.Emit(*this);
                    }
                  })),
     ...);
  }

  bool IsFull() {
    return std::all_of(std::begin(set_map_), std::end(set_map_),
                       [](auto v) { return v; });
  }

  Event<void(CumulativeEvent const& event)> res_event_;
  std::array<Subscription, Count> subscriptions_;
  std::array<bool, Count> set_map_{};
  std::array<T, Count> values_;
};

/**
 * \brief Specialization for void
 */
template <std::size_t Count>
class CumulativeEvent<void, Count> {
 public:
  CumulativeEvent() = default;

  template <typename... TSignatures>
  explicit CumulativeEvent(
      EventSubscriber<TSignatures>&&... event_subscribers) {
    Connect(std::move(event_subscribers)...);
  }

  template <typename TFunc>
  auto Subscribe(TFunc&& func) {
    return EventSubscriber{res_event_}.Subscribe(std::forward<TFunc>(func));
  }

  template <typename... EventSubscribers>
  void Connect(EventSubscribers&&... event_subscribers) {
    static_assert(sizeof...(EventSubscribers) == Count,
                  "Count of event subscribers should be same as Count");

    ConnectImpl(std::make_index_sequence<Count>(),
                std::forward<EventSubscribers>(event_subscribers)...);
  }

 private:
  template <typename... EventSubscribers, std::size_t... Is>
  void ConnectImpl(std::index_sequence<Is...>,
                   EventSubscribers&&... event_subscribers) {
    ((subscriptions_[Is] = std::forward<EventSubscribers>(event_subscribers)
                               .Subscribe([this](auto&&...) {
                                 set_map_[Is] = true;
                                 if (IsFull()) {
                                   res_event_.Emit();
                                 }
                               })),
     ...);
  }

  bool IsFull() {
    return std::all_of(std::begin(set_map_), std::end(set_map_),
                       [](auto v) { return v; });
  }

  Event<void()> res_event_;
  std::array<Subscription, Count> subscriptions_;
  std::array<bool, Count> set_map_{};
};

namespace events_internal {
template <typename TFunc, typename EvetSubscriber, typename Enable = void>
struct HandlerResult;

template <typename TFunc, typename... Args>
struct HandlerResult<TFunc, EventSubscriber<void(Args...)>,
                     std::enable_if_t<std::is_invocable_v<TFunc, Args...>>> {
  using Ret = std::invoke_result_t<TFunc, Args...>;
};
}  // namespace events_internal

template <typename... TSignatures>
CumulativeEvent(EventSubscriber<TSignatures>&&... event_subscribers)
    -> CumulativeEvent<void, sizeof...(TSignatures)>;

template <typename TFunc, typename... TSignatures>
CumulativeEvent(TFunc&& func,
                EventSubscriber<TSignatures>&&... event_subscribers)
    -> CumulativeEvent<
        std::common_type_t<typename events_internal::HandlerResult<
            TFunc, EventSubscriber<TSignatures>>::Ret...>,
        sizeof...(TSignatures)>;

}  // namespace ae

#endif  // AETHER_EVENTS_CUMULATIVE_EVENT_H_
