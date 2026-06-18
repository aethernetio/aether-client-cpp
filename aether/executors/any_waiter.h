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

#ifndef AETHER_EXECUTORS_ANY_WAITER_H_
#define AETHER_EXECUTORS_ANY_WAITER_H_

#include <memory>
#include <utility>
#include <optional>
#include <concepts>
#include <type_traits>

#include <stdexec/execution.hpp>

#include "aether-miscpp/types/result.h"
#include "aether-miscpp/meta/ignore_t.h"
#include "aether/executors/waiter_traits.h"
#include "aether/executors/async_context.h"
#include "aether/executors/scheduler_on_tasks.h"

namespace ae::ex {
namespace any_waiter_internal {
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

class OpStateBase {
 public:
  virtual ~OpStateBase() = default;
  virtual void start() = 0;
};

template <AsyncContext AC, typename ResultType, typename Sender, typename Cb>
class AnyOpState final : public OpStateBase {
 public:
  using state = State<AC, ResultType, Cb>;
  using receiver = Receiver<state>;
  using op_state = stdexec::connect_result_t<Sender, receiver>;

  AnyOpState(AC const& ac, Sender&& sender, Cb&& cb)
      : state_{.ac = ac, .wait_result = {}, .cb = std::move(cb)},
        op_state_{
            stdexec::connect(std::move(sender), receiver{.state = &state_})} {}

  void start() override { op_state_.start(); }

 private:
  state state_;
  op_state op_state_;
};
}  // namespace any_waiter_internal

template <typename... Sigs>
class AnyWaiter {
 public:
  using CompletionsTraitsImpl = CompletionsTraits<Sigs...>;
  using ValueType = CompletionsTraitsImpl::ValueType;
  using ErrorType = CompletionsTraitsImpl::ErrorType;
  using ResultType = std::conditional_t<
      !std::is_same_v<Ignore, typename CompletionsTraitsImpl::ErrorType>,
      Result<ValueType, ErrorType>, ValueType>;

  template <AsyncContext AC, stdexec::sender Sender, typename Cb>
    requires(std::invocable<Cb, std::optional<ResultType> &&> ||
             std::invocable<Cb, std::optional<ResultType>&>)
  AnyWaiter(AC const& ac, Sender&& sender, Cb&& cb)
      : op_state_{std::make_unique<
            any_waiter_internal::AnyOpState<AC, ResultType, Sender, Cb>>(
            ac, std::forward<Sender>(sender), std::forward<Cb>(cb))} {
    op_state_->start();
  }

 private:
  // type-erased op state
  std::unique_ptr<any_waiter_internal::OpStateBase> op_state_;
};

}  // namespace ae::ex

#if AE_TESTS
#  include "tests/inline.h"

#  include "aether/ae_context.h"

namespace test::any_waiter_h {
using namespace ae;      // NOLINT
using namespace ae::ex;  // NOLINT

AE_TEST_INLINE(test_Callables) {
  // used only for formal construction tests, not for runtime
  AeContext* ae_context{};
  using any_waiter = AnyWaiter<stdexec::set_value_t(int)>;
  // test if value callback is accepted
  using w_val = decltype(any_waiter{*ae_context, stdexec::just(1),
                                    [](std::optional<int> r) { (void)r; }});
  static_assert(std::is_same_v<w_val, any_waiter>);
  // test if const ref callback is accepted
  using w_cons_ref =
      decltype(any_waiter{*ae_context, stdexec::just(1),
                          [](std::optional<int> const& r) { (void)r; }});
  static_assert(std::is_same_v<w_cons_ref, any_waiter>);
  // test if ref callback is accepted
  using w_ref = decltype(any_waiter{*ae_context, stdexec::just(1),
                                    [](std::optional<int>& r) { (void)r; }});
  static_assert(std::is_same_v<w_ref, any_waiter>);

  // test if rvalue callback is accepted
  using w_rval = decltype(any_waiter{*ae_context, stdexec::just(1),
                                     [](std::optional<int>&& r) { (void)r; }});
  static_assert(std::is_same_v<w_rval, any_waiter>);

  TEST_PASS();
}
}  // namespace test::any_waiter_h

#endif

#endif  // AETHER_EXECUTORS_ANY_WAITER_H_
