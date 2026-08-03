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

#ifndef AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_
#define AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_

#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "aether-miscpp/types/result.h"

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>
DISABLE_WARNING_POP()

#include "aether/actions/action.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"

namespace ae::ex {
namespace action_wait_sender_internal {

template <typename E>
struct IsResultEvent : std::false_type {};
template <typename Ok, typename Err>
struct IsResultEvent<Event<void(Result<Ok, Err>)>> : std::true_type {};
template <typename Ok, typename Err>
struct IsResultEvent<EventSubscriber<void(Result<Ok, Err>)>> : std::true_type {
};

template <typename E>
concept ResultEvent = IsResultEvent<E>::value;

template <typename A>
concept ActionResultEvent = requires(A& a) {
  { a.result_event() } -> ResultEvent;
};

template <typename E>
struct ResultEventTrait {};

template <typename Ok, typename Err>
struct ResultEventTrait<Event<void(Result<Ok, Err>)>> {
  using type = Ok;
  using error = Err;
};

template <ActionResultEvent A, stdexec::receiver R>
class OpState {
  using ResultEvent =
      typename decltype(std::declval<A>().result_event())::EventType;
  using EventTrait = ResultEventTrait<ResultEvent>;
  using ResultType =
      Result<typename EventTrait::type, typename EventTrait::error>;

 public:
  constexpr OpState(A& action, R&& recv) noexcept
      : receiver_{std::move(recv)},
        event_sub_{action.result_event().Subscribe([&](auto&& r) noexcept {
          EventHandler(std::forward<decltype(r)>(r));
        })} {}

  OpState(OpState const&) = delete;
  OpState(OpState&&) noexcept = delete;
  auto& operator=(OpState const&) = delete;
  auto& operator=(OpState&&) noexcept = delete;

  constexpr void start() noexcept {
    // check if stop was requested
    auto token = stdexec::get_stop_token(stdexec::get_env(receiver_));
    if constexpr (std::is_same_v<decltype(token), std::stop_token>) {
      if (token.request_stop()) {
        event_sub_.Reset();
        stdexec::set_stopped(std::move(receiver_));
        return;
      }
    }
    // first check saved result before start
    if (res_) {
      HandleResult(std::move(res_.value()));
    } else
    // wait for event handler is called or stop is requested
    {
      started_ = true;
      if constexpr (std::is_same_v<decltype(token), std::stop_token>) {
        stop_cb_.emplace(token, StopCb{.self = this});
      }
    }
  }

 private:
  template <typename ResType>
  void EventHandler(ResType&& res) noexcept {
    if (started_) {
      HandleResult(std::forward<ResType>(res));
    } else {
      res_.emplace(std::forward<ResType>(res));
    }
  }

  template <typename ResType>
  void HandleResult(ResType&& res) noexcept {
    event_sub_.Reset();
    stop_cb_.reset();
    if (res) {
      stdexec::set_value(std::move(receiver_),
                         std::forward<ResType>(res).value());
    } else {
      stdexec::set_error(std::move(receiver_),
                         std::forward<ResType>(res).error());
    }
  }

  struct StopCb {
    void operator()() const noexcept {
      self->event_sub_.Reset();
      stdexec::set_stopped(std::move(self->receiver_));
    }
    OpState* self;
  };

  bool started_{false};
  R receiver_;
  Subscription event_sub_;
  std::optional<ResultType> res_;
  std::optional<std::stop_callback<StopCb>> stop_cb_;
};

template <ActionResultEvent A>
class Sender {
  using ResultEvent =
      typename decltype(std::declval<A>().result_event())::EventType;

  using EventTrait = ResultEventTrait<ResultEvent>;

 public:
  using sender_concept = stdexec::sender_t;

  template <typename S, typename... Args>
    requires(std::is_same_v<std::decay_t<S>, Sender>)
  static consteval auto get_completion_signatures()
      -> stdexec::completion_signatures<
          stdexec::set_value_t(typename EventTrait::type),
          stdexec::set_error_t(typename EventTrait::error),
          stdexec::set_stopped_t()> {
    return {};
  }

  constexpr explicit Sender(A& action) noexcept : action_{&action} {}

  template <stdexec::receiver R>
  constexpr auto connect(R&& r) noexcept {
    return OpState{*action_, std::forward<R>(r)};
  }

 private:
  A* action_;
};

struct ActionWait {
  template <ActionResultEvent A>
  constexpr auto operator()(A& action) const noexcept {
    return Sender{action};
  }
};
};  // namespace action_wait_sender_internal

static constexpr inline auto action_wait =
    action_wait_sender_internal::ActionWait{};

}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_EVENT_WAIT_SENDER_H_
