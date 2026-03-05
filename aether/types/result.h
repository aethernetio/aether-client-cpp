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

#ifndef AETHER_TYPES_RESULT_H_
#define AETHER_TYPES_RESULT_H_

#include <variant>
#include <cassert>
#include <utility>
#include <functional>
#include <type_traits>

#include "aether/meta/as_type.h"

namespace ae {
template <typename T, typename E>
class Result;

template <typename T>
struct IsResultType : std::false_type {};
template <typename V, typename E>
struct IsResultType<Result<V, E>> : std::true_type {};
template <typename T>
static inline constexpr bool IsResultType_v = IsResultType<T>::value;

template <typename T, typename V, typename E>
concept ResultType = requires(T t) {
  { IsResultType_v<T> };
  { std::same_as<V, typename T::value_type> };
  { std::same_as<E, typename T::error_type> };
};

template <typename T>
struct Ok {
  explicit Ok(T v) : value{std::move(v)} {}
  [[no_unique_address]] T value;
};

template <typename E>
struct Error {
  explicit Error(E v) : error{std::move(v)} {}
  [[no_unique_address]] E error;
};

template <typename T, typename E>
class Result {
 public:
  using value_type = T;
  using error_type = E;

  template <typename U>
    requires(std::is_same_v<T, U>)
  explicit Result(U&& value) : storage_{std::forward<U>(value)} {}

  template <typename EU>
    requires(std::is_same_v<E, EU>)
  explicit Result(EU&& error) : storage_{std::forward<EU>(error)} {}

  // made implicit intentionally
  Result(Ok<T>&& ok) : storage_{std::move(ok.value)} {}
  // made implicit intentionally
  Result(Error<E>&& error) : storage_{std::move(error.error)} {}

  // TODO: add monadic operations

  bool IsOk() const noexcept { return storage_.index() == 0; }
  bool IsErr() const noexcept { return storage_.index() == 1; }

  auto& value() & noexcept {
    assert(IsOk());
    return *get_value();
  }
  auto const& value() const& noexcept {
    assert(IsOk());
    return *get_value();
  }
  auto&& value() && noexcept {
    assert(IsOk());
    return std::move(*get_value());
  }

  auto& error() & noexcept {
    assert(IsErr());
    return *get_error();
  }
  auto const& error() const& noexcept {
    assert(IsErr());
    return *get_error();
  }
  auto&& error() && noexcept {
    assert(IsErr());
    return std::move(*get_error());
  }

  template <typename F, typename R = std::invoke_result_t<F, value_type&&>>
    requires(requires {
      // F must return result type
      { IsResultType_v<R> };
      { ResultType<R, typename R::value_type, error_type> };
    })
  auto Then(F&& f) && -> typename std::invoke_result_t<F, value_type&&> {
    if (IsOk()) {
      return std::invoke(std::forward<F>(f), std::move(*get_value()));
    }
    return Error{std::move(*get_error())};
  }

  template <typename F, typename R = std::invoke_result_t<F>>
    requires(requires {
      // F must return result type
      { IsResultType_v<R> };
      { ResultType<R, typename R::value_type, error_type> };
    })
  auto Then(F&& f) && -> std::invoke_result_t<F> {
    if (IsOk()) {
      return std::invoke(std::forward<F>(f));
    }
    return Error{std::move(*get_error())};
  }

  template <typename FE, typename R = std::invoke_result_t<FE, error_type&&>>
    requires(requires {
      // FE must return result type
      { IsResultType_v<R> };
      { ResultType<R, value_type, typename R::error_type> };
    })
  auto Else(FE&& f) && -> typename std::invoke_result_t<FE, error_type&&> {
    if (IsErr()) {
      return std::invoke(std::forward<FE>(f), std::move(*get_error()));
    }
    return Ok{std::move(*get_value())};
  }

  template <typename FE, typename R = std::invoke_result_t<FE>>
    requires(requires {
      // FE must return result type
      { IsResultType_v<R> };
      { ResultType<R, value_type, typename R::error_type> };
    })
  auto Else(FE&& f) && -> typename std::invoke_result_t<FE> {
    if (IsErr()) {
      return std::invoke(std::forward<FE>(f));
    }
    return Ok{std::move(*get_value())};
  }

 private:
  value_type* get_value() & { return std::get_if<0>(&storage_); }
  value_type* get_value() const& { return std::get_if<0>(&storage_); }
  error_type* get_error() & { return std::get_if<1>(&storage_); }
  error_type* get_error() const& { return std::get_if<1>(&storage_); }

  std::variant<value_type, error_type> storage_;
};
}  // namespace ae

#define TRY_VALUE(VAR_NAME, ...)                            \
  auto _RES_##VAR_NAME = __VA_ARGS__;                       \
  if (_RES_##VAR_NAME.IsErr()) {                            \
    return ::ae::Error{std::move(_RES_##VAR_NAME).error()}; \
  }                                                         \
  auto VAR_NAME = _RES_##VAR_NAME.value()

#define TRY_RESULT(...)                             \
  {                                                 \
    auto _RES_ = __VA_ARGS__;                       \
    if (_RES_.IsErr()) {                            \
      return ::ae::Error{std::move(_RES_).error()}; \
    }                                               \
  }

#if AE_TESTS

#  include "tests/inline.h"

namespace test::result_h_ {
using ae::Error;
using ae::Ok;
using ae::Result;

struct G {};
struct E {};

AE_TEST_INLINE(test_Result) {
  auto res = Result<G, E>{G{}};
  // check if concept works
  static_assert(ae::ResultType<decltype(res), G, E>);
  TEST_ASSERT_TRUE(res.IsOk());

  auto res_e = Result<G, E>{E{}};
  TEST_ASSERT_TRUE(res_e.IsErr());
}

AE_TEST_INLINE(test_Monadic) {
  auto ret_good = []() -> Result<G, E> { return Ok{G{}}; };
  auto ret_bad = []() -> Result<G, E> { return Error{E{}}; };

  auto res_g = ret_good().Then(ret_good);
  TEST_ASSERT_TRUE(res_g.IsOk());

  auto res_e = ret_good().Then(ret_bad);
  TEST_ASSERT_TRUE(res_e.IsErr());

  auto res_g2 = ret_bad().Else(ret_good);
  TEST_ASSERT_TRUE(res_g2.IsOk());
  auto res_e2 = ret_bad().Else(ret_bad);
  TEST_ASSERT_TRUE(res_e2.IsErr());

  auto res_cg = ret_good().Then(ret_good).Else(ret_bad);
  TEST_ASSERT_TRUE(res_cg.IsOk());
  auto res_cg2 = ret_good().Then(ret_bad).Else(ret_good);
  TEST_ASSERT_TRUE(res_cg2.IsOk());

  auto res_ce = ret_bad().Then(ret_good).Else(ret_bad);
  TEST_ASSERT_TRUE(res_ce.IsErr());
  auto res_ce2 = ret_bad().Else(ret_good).Else(ret_bad);
  TEST_ASSERT_TRUE(res_ce2.IsOk());
}

AE_TEST_INLINE(test_Macros) {
  auto try_value = [](int v, int n) -> Result<G, E> {
    if (v > n) {
      return Ok{G{}};
    }
    return Error{E{}};
  };

  auto test_good = [&]() -> Result<G, E> {
    TRY_VALUE(rg, try_value(12, 10));
    return Ok{rg};
  }();
  TEST_ASSERT_TRUE(test_good.IsOk());

  auto test_bad = [&]() -> Result<G, E> {
    TRY_VALUE(rg, try_value(10, 12));
    return Ok{rg};
  }();
  TEST_ASSERT_TRUE(test_bad.IsErr());

  auto test_good_res = [&]() -> Result<G, E> {
    TRY_RESULT(try_value(42, 5));
    return Ok{G{}};
  }();
  TEST_ASSERT_TRUE(test_good_res.IsOk());

  auto test_bad_res = [&]() -> Result<G, E> {
    TRY_RESULT(try_value(1, 105));
    return Ok{G{}};
  }();
  TEST_ASSERT_TRUE(test_bad_res.IsErr());
}

}  // namespace test::result_h_

#endif
#endif  // AETHER_TYPES_RESULT_H_
