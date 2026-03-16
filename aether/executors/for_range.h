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

#ifndef AETHER_EXECUTORS_FOR_RANGE_H_
#define AETHER_EXECUTORS_FOR_RANGE_H_

#include <utility>
#include <optional>
#include <type_traits>

#include "third_party/stdexec/include/stdexec/execution.hpp"

#include "aether/types/iterator.h"

namespace ae::ex {
namespace for_range_internal {
struct ContinueT {};

template <typename T>
struct IsContinue : std::false_type {};
template <>
struct IsContinue<ContinueT> : std::true_type {};
template <typename T>
static constexpr inline bool IsContinue_v = IsContinue<T>::value;

template <typename Receiver>
struct OperationBase {
  explicit constexpr OperationBase(Receiver&& r) noexcept
      : recv{std::move(r)} {}

  virtual constexpr void Repeat() noexcept {}

  Receiver recv;

 protected:
  ~OperationBase() noexcept = default;
};

template <typename R>
struct Receiver {
  using receiver_concept = stdexec::receiver_t;

  template <typename U>
  constexpr void set_value(U&& val) && noexcept {
    using value_type = std::decay_t<U>;
    if constexpr (IsContinue_v<value_type>) {
      operation->Repeat();
    } else {
      stdexec::set_value(std::move(operation->recv), std::forward<U>(val));
    }
  }

  template <typename E>
  constexpr void set_error(E&& e) && noexcept {
    using value_type = std::decay_t<E>;
    if constexpr (IsContinue_v<value_type>) {
      operation->Repeat();
    } else {
      stdexec::set_error(std::move(operation->recv), std::forward<E>(e));
    }
  }

  constexpr void set_stopped() && noexcept {
    stdexec::set_stopped(std::move(operation->recv));
  }

  [[nodiscard]]
  constexpr auto get_env() const& noexcept {
    return stdexec::get_env(operation->recv);
  }

  OperationBase<R>* operation;
};

template <typename R, typename Iter, typename Fn>
class Operation final : public OperationBase<R> {
 public:
  using value_type = typename Iter::value_type;
  using sender_type = std::decay_t<std::invoke_result_t<Fn, value_type>>;
  using op_type = stdexec::connect_result_t<sender_type, Receiver<R>>;

  constexpr Operation(R&& recv, Iter&& iter, Fn&& fn) noexcept
      : OperationBase<R>{std::move(recv)},
        iter_{std::move(iter)},
        fn_{std::move(fn)} {}

  constexpr void start() & noexcept { Repeat(); }

  constexpr void Repeat() noexcept override {
    op_.reset();
    if (auto v = iter_.Next(); !!v) {
      op_.__emplace_from(stdexec::connect, std::invoke(fn_, *v),
                         Receiver<R>{.operation = this});
      stdexec::start(*op_);
    } else {
      // just finish with empty value
      stdexec::set_stopped(std::move(OperationBase<R>::recv));
    }
  }

 private:
  Iter iter_;
  Fn fn_;
  stdexec::__optional<op_type> op_;
};

/**
 * \brief Run a for range loop on begin,end and invoke fn with it's elements.
 * fn should return the sender which then connected and invoked.
 * sender should return it's value with Finish and Continue wrappers
 */
template <IsIter Iter, typename Fn>
class ForRangeSender {
 public:
  using sender_concept = stdexec::sender_t;

  using value_type = typename Iter::value_type;
  using sender_type = std::invoke_result_t<Fn, value_type>;

  constexpr ForRangeSender(Iter&& iter, Fn&& fn) noexcept
      : iter_{std::move(iter)}, fn_{std::move(fn)} {}

  template <typename R>
  constexpr auto connect(R&& recv) && noexcept {
    return Operation{std::forward<R>(recv), std::move(iter_), std::move(fn_)};
  }

  template <typename Sender, typename... Env>
  static constexpr auto get_completion_signatures() noexcept {
    using sender_completions =
        decltype(stdexec::get_completion_signatures<sender_type, Env...>());

    return stdexec::__transform_completion_signatures(
        sender_completions{}, transform_values, transform_errors,
        stdexec::__keep_completion<stdexec::set_stopped_t>{},
        stdexec::completion_signatures<stdexec::set_stopped_t()>{});
  }

 private:
  static constexpr auto transform_values = []<typename... Args>() {
    auto convert = []<typename Arg>() {
      if constexpr (IsContinue_v<Arg>) {
        return stdexec::completion_signatures<>{};
      } else {
        return stdexec::completion_signatures<stdexec::set_value_t(Arg)>{};
      }
    };

    return stdexec::__concat_completion_signatures(
        convert.template operator()<Args>()...);
  };

  static constexpr auto transform_errors = []<typename... Args>() {
    auto convert = []<typename Arg>() {
      if constexpr (IsContinue_v<Arg>) {
        return stdexec::completion_signatures<>{};
      } else {
        return stdexec::completion_signatures<stdexec::set_error_t(Arg)>{};
      }
    };

    return stdexec::__concat_completion_signatures(
        convert.template operator()<Args>()...);
  };

  Iter iter_;
  Fn fn_;
};

struct MakeForRangeSender {
  template <IsIter Iter, typename Fn>
    requires(std::invocable<Fn, typename Iter::value_type>)
  constexpr auto operator()(Iter&& iter, Fn&& fn) const noexcept {
    return ForRangeSender{std::forward<Iter>(iter), std::forward<Fn>(fn)};
  }
};

}  // namespace for_range_internal

static constexpr inline auto for_continue = for_range_internal::ContinueT{};

static constexpr inline auto for_range =
    for_range_internal::MakeForRangeSender{};

}  // namespace ae::ex

#if AE_TESTS
#  include "tests/inline.h"

#  include <array>

#  include "aether/executors/make_sender.h"

namespace tests::for_range_h {
namespace ex {
using namespace stdexec;  // NOLINT
using namespace ae::ex;   // NOLINT
}  // namespace ex
AE_TEST_INLINE(test_ForRangeSender) {
  int counter = 0;
  auto for_range = ex::for_range(ae::Range{0, 4}, [&](int i) noexcept {
    return ex::just(i) | ex::let_value([&](int v) noexcept {
             counter++;
             return ex::make_sender<
                 ex::set_value_t(int),
                 ex::set_error_t(decltype(ex::for_continue))>([v](auto&& r) {
               if (v >= 3) {
                 ex::set_value(std::forward<decltype(r)>(r), v);
               } else {
                 ex::set_error(std::forward<decltype(r)>(r), ex::for_continue);
               }
             });
           });
  });

  auto v = ex::sync_wait(std::move(for_range));

  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL(3, std::get<0>(v.value()));
  TEST_ASSERT_EQUAL(4, counter);
}

AE_TEST_INLINE(test_ReturnFirstValueX2) {
  std::array values = {12, 42, 33, 14};
  auto for_range = ex::for_range(
      ae::Iter{values.begin(), values.end()}, [](auto const& i) noexcept {
        return ex::just(i) |
               ex::then([](auto const& i) noexcept { return i * 2; });
      });
  auto v = ex::sync_wait(std::move(for_range));
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL(24, std::get<0>(v.value()));
}

}  // namespace tests::for_range_h

#endif

#endif  // AETHER_EXECUTORS_FOR_RANGE_H_
