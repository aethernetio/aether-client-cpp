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

#ifndef AETHER_EXECUTORS_SYNC_WAITER_H_
#define AETHER_EXECUTORS_SYNC_WAITER_H_

#include <optional>

#include "aether/warning_disable.h"

// IWYU pragma: begin_exports
DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include <stdexec/execution.hpp>
DISABLE_WARNING_POP()

#include "aether/types/result.h"
#include "aether/executors/waiter_traits.h"

namespace ae::ex {
namespace sync_waiter_internal {
struct Env {
  [[nodiscard]]
  constexpr auto query(stdexec::get_scheduler_t) const noexcept {
    return rl->get_scheduler();
  }

  [[nodiscard]]
  constexpr auto query(stdexec::get_delegation_scheduler_t) const noexcept {
    return rl->get_scheduler();
  }

  [[nodiscard]]
  constexpr auto query(stdexec::__root_t) const noexcept {
    return true;
  }

  stdexec::run_loop* rl;
};

template <typename ValueType, typename ErrorType>
struct State {
  using value_type = ValueType;
  using error_type = ErrorType;

  std::optional<Result<value_type, error_type>> wait_result;
  stdexec::run_loop rl;
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

  decltype(auto) get_env() const noexcept { return Env{.rl = &state->rl}; }

  constexpr void Finish() noexcept { state->rl.finish(); }

  State* state;
};

template <stdexec::sender S>
constexpr auto SyncWait(S&& s) noexcept {
  using env_type = Env;
  using completions =
      decltype(stdexec::get_completion_signatures<S, env_type>());
  using completions_traits = CompletionsTraitsImpl<completions>;

  using StateType = State<typename completions_traits::ValueType,
                          typename completions_traits::ErrorType>;

  StateType state;
  auto op = stdexec::connect(std::forward<S>(s), Receiver{.state = &state});
  stdexec::start(op);

  state.rl.run();

  return state.wait_result;
}

}  // namespace sync_waiter_internal

using sync_waiter_internal::SyncWait;
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_SYNC_WAITER_H_
