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
#include "aether/meta/ignore_t.h"
#include "aether/meta/type_list.h"
#include "aether/executors/scheduler_on_tasks.h"

#include "third_party/stdexec/include/stdexec/execution.hpp"

namespace ae::ex {

namespace async_wait_internal {
namespace {

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

template <typename T>
struct FilterExceptionPtr {
  static constexpr bool value =
      std::is_same_v<std::decay_t<T>, std::exception_ptr>;
};

/**
 * Expand value list.
 * It may be a variant of values
 * Values may be a tuples or single values
 * Or it may be a single value or void
 */
template <typename>
struct ExpandTuple;
template <>
struct ExpandTuple<TypeList<>> {
  using type = Ignore;
};
template <typename T>
struct ExpandTuple<TypeList<T>> {
  using type = T;
};
template <typename... T>
struct ExpandTuple<TypeList<T...>> {
  using type = std::tuple<T...>;
};

template <typename>
struct ExpandVariant;
template <>
struct ExpandVariant<TypeList<>> {
  using type = Ignore;
};
template <typename T>
struct ExpandVariant<TypeList<T>> {
  using type = ExpandTuple<T>::type;
};
template <typename... T>
struct ExpandVariant<TypeList<T...>> {
  using type = TypeListToTemplate_t<
      std::variant,
      typename FilterList<IsIgnore,
                          TypeList<typename ExpandTuple<T>::type...>>::type>;
};
}  // namespace

template <stdexec::sender Sender, typename... Env>
struct SenderTraits {
  static constexpr auto cs =
      stdexec::get_completion_signatures<Sender, Env...>();
  using ValueList =
      stdexec::__value_types_t<decltype(cs), stdexec::__q<TypeList>,
                               stdexec::__q<TypeList>>;
  using ErrorList =
      stdexec::__error_types_t<decltype(cs), stdexec::__q<TypeList>,
                               stdexec::__q<stdexec::__msingle>>;

  using ValueType = ExpandVariant<ValueList>::type;

  using FilteredErrorList = FilterList<FilterExceptionPtr, ErrorList>::type;

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
                                 Callback&& cb) noexcept
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

#if AE_TESTS

#  include "tests/inline.h"

namespace tests::async_wait_h {

template <typename... Sigs>
struct NotSender {
  using sender_concept = stdexec::sender_t;

  template <typename...>
  static consteval auto get_completion_signatures() {
    return stdexec::completion_signatures<Sigs...>{};
  }
};

using namespace ae::ex::async_wait_internal;  // NOLINT
using namespace ae;                           // NOLINT

AE_TEST_INLINE(test_SenderTraits) {
  using sender1 =
      NotSender<stdexec::set_value_t(int), stdexec::set_error_t(int)>;

  using traits1 = SenderTraits<sender1, Env>;

  static_assert(std::is_same_v<TypeList<TypeList<int>>, traits1::ValueList>);
  static_assert(std::is_same_v<int, traits1::ValueType>);
  static_assert(std::is_same_v<int, traits1::ErrorType>);

  using sender2 =
      NotSender<stdexec::set_value_t(int, float), stdexec::set_error_t(int)>;
  using traits2 = SenderTraits<sender2, Env>;
  static_assert(
      std::is_same_v<TypeList<TypeList<int, float>>, traits2::ValueList>);
  static_assert(std::is_same_v<std::tuple<int, float>, traits2::ValueType>);
  static_assert(std::is_same_v<int, traits2::ErrorType>);

  using sender3 = NotSender<stdexec::set_value_t(), stdexec::set_error_t(int)>;
  using traits3 = SenderTraits<sender3, Env>;
  static_assert(std::is_same_v<TypeList<TypeList<>>, traits3::ValueList>);
  static_assert(std::is_same_v<Ignore, traits3::ValueType>);
  static_assert(std::is_same_v<int, traits3::ErrorType>);

  using sender4 =
      NotSender<stdexec::set_value_t(int), stdexec::set_value_t(float),
                stdexec::set_error_t(int)>;
  using traits4 = SenderTraits<sender4, Env>;
  static_assert(std::is_same_v<TypeList<TypeList<int>, TypeList<float>>,
                               traits4::ValueList>);
  static_assert(std::is_same_v<std::variant<int, float>, traits4::ValueType>);
  static_assert(std::is_same_v<int, traits4::ErrorType>);

  using sender5 =
      NotSender<stdexec::set_value_t(), stdexec::set_value_t(int, float),
                stdexec::set_value_t(bool), stdexec::set_error_t(int),
                stdexec::set_error_t(std::exception_ptr)>;
  using traits5 = SenderTraits<sender5, Env>;
  static_assert(
      std::is_same_v<TypeList<TypeList<>, TypeList<int, float>, TypeList<bool>>,
                     traits5::ValueList>);
  static_assert(std::is_same_v<std::variant<std::tuple<int, float>, bool>,
                               traits5::ValueType>);
  static_assert(std::is_same_v<int, traits5::ErrorType>);

  using sender6 = NotSender<stdexec::set_error_t(int),
                            stdexec::set_error_t(std::exception_ptr)>;
  using traits6 = SenderTraits<sender6, Env>;
  static_assert(std::is_same_v<TypeList<>, traits6::ValueList>);
  static_assert(std::is_same_v<Ignore, traits6::ValueType>);
  static_assert(std::is_same_v<int, traits6::ErrorType>);

  TEST_PASS();
}
}  // namespace tests::async_wait_h
#endif  // AE_TESTS
#endif  // AETHER_EXECUTORS_ASYNC_WAIT_H_
