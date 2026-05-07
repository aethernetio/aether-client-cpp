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
#include "third_party/stdexec/include/stdexec/execution.hpp"
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

template <AsyncContext AC, typename ValueType, typename ErrorType, typename Cb>
struct State {
  using value_type = ValueType;
  using error_type = ErrorType;

  void Finish() { std::invoke(cb, std::move(wait_result)); }

  AC ac;
  std::optional<Result<value_type, error_type>> wait_result;
  Cb cb;
};

template <typename State>
struct Receiver {
  using receiver_concept = stdexec::receiver_t;
  using value_type = State::value_type;
  using error_type = State::error_type;

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
  constexpr void set_error(E&&... e) && noexcept {
    if constexpr (!IsIgnore_v<error_type>) {
      static_assert(std::is_constructible_v<error_type, E...>,
                    "Error must be constructible from argument list");
      state->wait_result.emplace(error_type{std::forward<E>(e)...});
    } else {
      static_assert((sizeof...(e) == 0), "Zero arguments are expected");
      state->wait_result.emplace(error_type{});
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
  using HandlerCb =
      SmallFunction<void(std::optional<Result<ValueType, ErrorType>>)>;

  using StateType = State<AC, ValueType, ErrorType, HandlerCb>;

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
