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

#ifndef AETHER_EXECUTORS_ASYNC_WAITER_H_
#define AETHER_EXECUTORS_ASYNC_WAITER_H_

#include <utility>
#include <optional>
#include <functional>

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>
DISABLE_WARNING_POP()

#include "aether/types/result.h"
#include "aether/types/small_function.h"

#include "aether/executors/waiter_traits.h"
#include "aether/executors/async_context.h"
#include "aether/executors/scheduler_on_tasks.h"

namespace ae::ex {
namespace async_waiter_internal {
template <AsyncContext AC>
struct Env {
 public:
  [[nodiscard]]
  constexpr auto query(stdexec::get_scheduler_t) const noexcept {
    return SchedulerOnTasks{ac};
  }
  [[nodiscard]]
  constexpr auto query(stdexec::get_delegation_scheduler_t) const noexcept {
    return SchedulerOnTasks{ac};
  }
  [[nodiscard]]
  constexpr auto query(stdexec::__root_t) const noexcept {
    return true;
  }

  AC const& ac;
};

template <AsyncContext AC, typename ResultType, typename Cb>
struct State {
  using result_type = ResultType;

  void Finish() { std::invoke(cb, std::move(wait_result)); }

  AC ac;
  std::optional<result_type> wait_result;
  Cb cb;
};

template <typename State>
struct Receiver {
  using receiver_concept = stdexec::receiver_t;

  template <typename T>
  struct ValueTypeSelector {
    using type = T;
  };

  template <typename T1, typename T2>
  struct ValueTypeSelector<Result<T1, T2>> {
    using type = T1;
  };

  template <typename T>
  struct ErrorTypeSelector {
    using type = Ignore;
  };

  template <typename T1, typename T2>
  struct ErrorTypeSelector<Result<T1, T2>> {
    using type = T2;
  };

  using value_type = ValueTypeSelector<typename State::result_type>::type;
  using error_type = ErrorTypeSelector<typename State::result_type>::type;

  template <typename... U>
  constexpr void set_value(U&&... u) && noexcept {
    if constexpr (!IsIgnore_v<value_type>) {
      static_assert(std::is_constructible_v<value_type, U...>,
                    "Must be constractible from argument list");
      state->wait_result.emplace(value_type{std::forward<U>(u)...});
    } else {
      static_assert((sizeof...(u) == 0), "Zero arguments are expected");
      state->wait_result.emplace(value_type{});
    }
    Finish();
  }

  constexpr void set_error(std::exception_ptr&& e) && noexcept {
    // exceptions are not supported
    (void)std::move(e);
    std::abort();
  }

  template <typename... E>
  constexpr void set_error([[maybe_unused]] E&&... e) && noexcept {
    if constexpr (!IsIgnore_v<error_type>) {
      static_assert(std::is_constructible_v<error_type, E...>,
                    "Error must be constructible from argument list");
      state->wait_result.emplace(error_type{std::forward<E>(e)...});
    } else {
      // ignore error does not supported
      std::abort();
    }
    Finish();
  }

  constexpr void set_stopped() && noexcept { Finish(); }

  decltype(auto) get_env() const noexcept { return Env{.ac = state->ac}; }

  constexpr void Finish() noexcept { state->Finish(); }

  State* state;
};

template <AsyncContext AC, stdexec::sender S>
class AsyncWaiter {
  using Completions =
      decltype(stdexec::get_completion_signatures<S, Env<AC>>());
  using CompletionsTraits = CompletionsTraitsImpl<Completions>;
  using ValueType = CompletionsTraits::ValueType;
  using ErrorType = CompletionsTraits::ErrorType;
  using ResultType = std::conditional_t<
      !std::is_same_v<Ignore, typename CompletionsTraits::ErrorType>,
      Result<ValueType, ErrorType>, ValueType>;

  using HandlerCb = SmallFunction<void(std::optional<ResultType>)>;
  using StateType = State<AC, ResultType, HandlerCb>;
  using OpState = stdexec::connect_result_t<S, Receiver<StateType>>;

 public:
  AsyncWaiter(AC const& ac, S&& s, HandlerCb&& handler_cb)
      : state_{
            .ac = ac, .wait_result = std::nullopt, .cb = std::move(handler_cb)},
        op_state_{stdexec::connect(std::move(s), Receiver{&state_})} {
    // run operation and wait for result asynchronously
    stdexec::start(op_state_);
  }

 private:
  StateType state_;
  OpState op_state_;
};
}  // namespace async_waiter_internal

using async_waiter_internal::AsyncWaiter;
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_ASYNC_WAITER_H_
