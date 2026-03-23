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

#ifndef AETHER_EXECUTORS_ASYNC_WAIT_H_
#define AETHER_EXECUTORS_ASYNC_WAIT_H_

#include <cstdlib>

#include <tuple>
#include <variant>
#include <optional>
#include <functional>
#include <type_traits>

#include "aether/types/result.h"
#include "aether/meta/type_list.h"
#include "aether/executors/scheduler_on_tasks.h"

#include "third_party/stdexec/include/stdexec/execution.hpp"

namespace ae::ex {

namespace async_wait_internal {
template <template <typename> typename Filter, typename T>
struct FilterList;

template <template <typename> typename Filter, typename T, typename... Ts>
struct FilterList<Filter, TypeList<T, Ts...>> {
  using type = JoinedTypeList_t<
      std::conditional_t<!Filter<T>::value, TypeList<T>, TypeList<>>,
      typename FilterList<Filter, TypeList<Ts...>>::type>;
};

template <template <typename> typename Filter>
struct FilterList<Filter, TypeList<>> {
  using type = TypeList<>;
};

template <stdexec::sender Sender, typename Env>
struct SenderTraits {
  static constexpr auto cs = stdexec::get_completion_signatures<Sender, Env>();
  using ValueList =
      stdexec::__value_types_t<decltype(cs), stdexec::__q<TypeList>,
                               stdexec::__q<stdexec::__msingle>>;
  using ErrorList =
      stdexec::__error_types_t<decltype(cs), stdexec::__q<TypeList>,
                               stdexec::__q<stdexec::__msingle>>;

  static_assert((TypeListSize_v<ValueList> > 0),
                "Value list must not be empty");

  using ValueType =
      std::conditional_t<(TypeListSize_v<ValueList> > 1),
                         TypeListToTemplate_t<std::tuple, ValueList>,
                         TypeAt_t<0, ValueList>>;

  template <typename T>
  struct FilterExceptionPtr {
    static constexpr bool value =
        std::is_same_v<std::decay_t<T>, std::exception_ptr>;
  };

  using FilteredErrorList =
      typename FilterList<FilterExceptionPtr, ErrorList>::type;

  using ErrorType =
      std::conditional_t<(TypeListSize_v<FilteredErrorList> > 1),
                         TypeListToTemplate_t<std::variant, FilteredErrorList>,
                         TypeAt_t<0, FilteredErrorList>>;
};

class WaiterBase {
 public:
  constexpr explicit WaiterBase(AeContext const& context) noexcept
      : context_{context} {}

  [[nodiscard]] constexpr auto get_scheduler() const noexcept {
    return SchedulerOnTasks{context_};
  }

 private:
  AeContext context_;
};

struct Env {
 public:
  [[nodiscard]]
  constexpr auto query(stdexec::get_scheduler_t) const noexcept {
    return waiter->get_scheduler();
  }

  [[nodiscard]]
  constexpr auto query(stdexec::get_delegation_scheduler_t) const noexcept {
    return waiter->get_scheduler();
  }

  [[nodiscard]]
  constexpr auto query(stdexec::__root_t) const noexcept -> bool {
    return true;
  }

  WaiterBase* waiter;
};

template <typename State>
class Receiver {
 public:
  using receiver_concept = stdexec::receiver_t;
  using value_type = typename State::value_type;
  using error_type = typename State::error_type;

  constexpr explicit Receiver(State& state) : state_{&state} {}

  template <typename... U>
  constexpr void set_value(U&&... u) && noexcept {
    static_assert(std::is_constructible_v<value_type, U...>,
                  "Must be constractible from argument list");
    state_->wait_result.emplace(value_type{std::forward<U>(u)...});
    state_->Finish();
  }

  template <typename E>
  constexpr void set_error(E&& e) && noexcept {
    if constexpr (std::is_same_v<std::decay_t<E>, std::exception_ptr>) {
      // exceptions does not supported
      std::abort();
    } else {
      static_assert(std::is_constructible_v<error_type, std::decay_t<E>>,
                    "Error type mismatch");
      state_->wait_result.emplace(error_type{std::forward<E>(e)});
      state_->Finish();
    }
  }

  constexpr void set_stopped() && noexcept { state_->Finish(); }

  constexpr auto get_env() & noexcept { return state_->env; }

 private:
  State* state_;
};

template <typename ValueType, typename ErrorType, typename Cb>
struct State {
  using value_type = ValueType;
  using error_type = ErrorType;

  void Finish() { std::invoke(cb, std::move(wait_result)); }

  std::optional<Result<ValueType, ErrorType>> wait_result;
  Cb cb;
  Env env;
};

}  // namespace async_wait_internal

template <stdexec::sender Sender, typename Callback>
class AsyncWaiter : async_wait_internal::WaiterBase {
  using SenderTraits =
      async_wait_internal::SenderTraits<Sender, async_wait_internal::Env>;
  using State =
      async_wait_internal::State<typename SenderTraits::ValueType,
                                 typename SenderTraits::ErrorType, Callback>;
  using WaitResultType = decltype(std::declval<State>().wait_result);
  using Receiver = async_wait_internal::Receiver<State>;
  using Operation = stdexec::connect_result_t<Sender, Receiver>;

  static_assert(std::is_invocable_v<Callback, WaitResultType&&>,
                "Callback must be invocable with result");

 public:
  explicit constexpr AsyncWaiter(AeContext const& context, Sender&& sender,
                                 Callback&& cb)
      : async_wait_internal::WaiterBase{context},
        state_{.wait_result = {}, .cb = std::move(cb), .env = {this}},
        op_{stdexec::connect(std::move(sender), Receiver{state_})} {
    stdexec::start(op_);
  }

 private:
  State state_;
  Operation op_;
};

template <stdexec::sender Sender, typename Callback>
auto MakeAsyncWaiter(AeContext const& context, Sender&& sender,
                     Callback&& callback) {
  return AsyncWaiter<std::decay_t<Sender>, std::decay_t<Callback>>{
      context, std::forward<Sender>(sender), std::forward<Callback>(callback)};
}
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_ASYNC_WAIT_H_
