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

#ifndef AETHER_ACTIONS_ACTION_EVENT_STATUS_H_
#define AETHER_ACTIONS_ACTION_EVENT_STATUS_H_

#include <tuple>
#include <utility>
#include <functional>
#include <type_traits>

#include "aether/common.h"
#include "aether/actions/update_status.h"

namespace ae {
/**
 * \brief The result action returns in it's event.
 */
template <typename TAction>
class ActionEventStatus {
 public:
  ActionEventStatus() = default;

  constexpr ActionEventStatus(TAction& action, UpdateStatus result)
      : action_{&action}, result_{result} {}

  AE_CLASS_COPY_MOVE(ActionEventStatus);

  template <typename Handler>
  ActionEventStatus OnResult(Handler&& handler) {
    if (result_.type == UpdateStatusType::kResult) {
      assert(action_ != nullptr);
      Invoke(std::forward<Handler>(handler));
      return {};
    }
    return *this;
  }

  template <typename Handler>
  ActionEventStatus OnError(Handler&& handler) {
    if (result_.type == UpdateStatusType::kError) {
      assert(action_ != nullptr);
      Invoke(std::forward<Handler>(handler));
      return {};
    }
    return *this;
  }

  template <typename Handler>
  ActionEventStatus OnStop(Handler&& handler) {
    if (result_.type == UpdateStatusType::kStop) {
      assert(action_ != nullptr);
      Invoke(std::forward<Handler>(handler));
      return {};
    }
    return *this;
  }

  TAction& action() const { return *action_; }
  UpdateStatus const& result() const { return result_; }

 private:
  template <typename Handler>
  void Invoke(Handler&& handler) {
    if constexpr (std::is_invocable_v<Handler, TAction&>) {
      assert(action_ != nullptr);
      std::invoke(std::forward<Handler>(handler), *action_);
    } else {
      static_assert(std::is_invocable_v<Handler>, "Handler must be invocable");
      std::invoke(std::forward<Handler>(handler));
    }
  }

  TAction* action_{nullptr};
  UpdateStatus result_{};
};

/**
 * \brief Handler for result type in action event status.
 */
template <typename F>
struct OnResult {
  template <typename T>
  auto operator()(ActionEventStatus<T> action_event_result) {
    return action_event_result.OnResult(func);
  }

  F func;
};

template <typename F>
OnResult(F&&) -> OnResult<F>;

/**
 * \brief Handler for error type in action event status.
 */
template <typename F>
struct OnError {
  template <typename T>
  auto operator()(ActionEventStatus<T> action_event_result) {
    return action_event_result.OnError(func);
  }

  F func;
};

template <typename F>
OnError(F&&) -> OnError<F>;

/**
 * \brief Handler for stop type in action event status.
 */
template <typename F>
struct OnStop {
  template <typename T>
  auto operator()(ActionEventStatus<T> action_event_result) {
    return action_event_result.OnStop(func);
  }

  F func;
};

template <typename F>
OnStop(F&&) -> OnStop<F>;

/**
 * \brief A generic action event result handler.
 */
template <typename... Handlers>
class ActionHandler {
  template <typename T>
  struct IsHandler : std::false_type {};
  template <typename T>
  struct IsHandler<OnResult<T>> : std::true_type {};
  template <typename T>
  struct IsHandler<OnError<T>> : std::true_type {};
  template <typename T>
  struct IsHandler<OnStop<T>> : std::true_type {};

  template <typename A, typename B>
  struct IsSameHandler : std::false_type {};
  template <typename Af, typename Bf>
  struct IsSameHandler<OnResult<Af>, OnResult<Bf>> : std::true_type {};
  template <typename Af, typename Bf>
  struct IsSameHandler<OnError<Af>, OnError<Bf>> : std::true_type {};
  template <typename Af, typename Bf>
  struct IsSameHandler<OnStop<Af>, OnStop<Bf>> : std::true_type {};

  template <typename H, typename... Hs>
  static constexpr bool HasDuplicate() {
    if constexpr (sizeof...(Hs) == 0) {
      return false;
    } else {
      return (IsSameHandler<H, Hs>::value || ...) || HasDuplicate<Hs...>();
    }
  }

 public:
  static_assert(sizeof...(Handlers) > 0, "There must be at least one handler");
  static_assert((IsHandler<Handlers>::value && ...),
                "Handlers must be OnResult, OnError or OnStop");
  static_assert(!HasDuplicate<Handlers...>(),
                "Handlers must not have duplicates");

  constexpr explicit ActionHandler(Handlers... hdlrs)
      : handlers_{std::move(hdlrs)...} {}

  template <typename TAction>
  void operator()(ActionEventStatus<TAction> action_event_result) {
    std::apply(
        [&action_event_result](Handlers&... h) {
          ((action_event_result = h(action_event_result)), ...);
        },
        handlers_);
  }

 private:
  std::tuple<Handlers...> handlers_;
};

}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_EVENT_STATUS_H_
