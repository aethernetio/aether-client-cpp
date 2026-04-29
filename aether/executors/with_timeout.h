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

#ifndef AETHER_EXECUTORS_WITH_TIMEOUT_H_
#define AETHER_EXECUTORS_WITH_TIMEOUT_H_

#include <utility>
#include <cassert>

#include "third_party/stdexec/include/stdexec/execution.hpp"

#include "aether/clock.h"
#include "aether/executors/async_context.h"

namespace ae::ex {
struct TimeoutError {};

namespace async_timeout_internal {
template <typename R>
class OpBase {
 public:
  constexpr explicit OpBase(R&& recv) noexcept : recv{std::move(recv)} {}
  virtual void Reset() noexcept {}
  virtual bool is_reset() noexcept { return false; }

  R recv;

 protected:
  ~OpBase() = default;
};

template <typename R>
struct Receiver {
  using receiver_concept = stdexec::receiver_t;

  constexpr void set_value(auto&&... v) && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      stdexec::set_value(std::move(op->recv), std::forward<decltype(v)>(v)...);
    }
  }
  constexpr void set_error(auto&& e) && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      stdexec::set_error(std::move(op->recv), std::forward<decltype(e)>(e));
    }
  }
  constexpr void set_stopped() && noexcept {
    if (!op->is_reset()) {
      op->Reset();
      stdexec::set_stopped(std::move(op->recv));
    }
  }
  constexpr auto get_env() const& noexcept {
    assert(!op->is_reset());
    return stdexec::get_env(op->recv);
  }

  OpBase<R>* op;
};

template <stdexec::sender Child, AsyncContext AC, stdexec::receiver R>
class Operation final : OpBase<R> {
 public:
  using receiver = Receiver<R>;
  using op_state = stdexec::connect_result_t<Child, receiver>;

  constexpr Operation(Child&& child, AC const& ac, Duration duration, R&& recv)
      : OpBase<R>{std::move(recv)},
        child_{std::move(child)},
        ac_{ac},
        duration_{std::move(duration)} {}

  constexpr void start() noexcept {
    op_.__emplace_from(stdexec::connect, std::move(child_),
                       Receiver{.op = this});
    ac_.scheduler().DelayedTask(
        [&]() noexcept {
          if (!is_reset()) {
            stdexec::set_error(std::move(OpBase<R>::recv), TimeoutError{});
            Reset();
          }
        },
        duration_);

    op_->start();
  }

  void Reset() noexcept override { is_reset_ = true; }
  bool is_reset() noexcept override { return is_reset_; }

 private:
  Child child_;
  AC ac_;
  Duration duration_;
  stdexec::__optional<op_state> op_;
  bool is_reset_{false};
};

template <stdexec::sender Child, AsyncContext AC>
class Sender {
 public:
  using sender_concept = stdexec::sender_t;

  template <typename S, typename... Env>
  static consteval auto get_completion_signatures() {
    constexpr auto chind_completions =
        stdexec::get_completion_signatures<Child, Env...>();
    return stdexec::__transform_completion_signatures(
        chind_completions, stdexec::__keep_completion<stdexec::set_value_t>{},
        stdexec::__keep_completion<stdexec::set_error_t>{},
        stdexec::__keep_completion<stdexec::set_stopped_t>{},
        // add error timeout
        stdexec::completion_signatures<stdexec::set_error_t(TimeoutError)>{});
  }

  constexpr Sender(Child&& child, AC const& ac, Duration duration) noexcept
      : child_{std::move(child)}, ac_{ac}, duration_{duration} {}

  template <stdexec::receiver R>
  constexpr auto connect(R&& recv) && noexcept {
    return Operation<Child, AC, R>{
        std::move(child_),
        ac_,
        duration_,
        std::forward<R>(recv),
    };
  }

 private:
  Child child_;
  AC ac_;
  Duration duration_;
};

struct WithAsyncTimeout {
  constexpr auto operator()(stdexec::sender auto&& sender,
                            AsyncContext auto const& context,
                            Duration duration) const noexcept {
    return Sender{std::forward<decltype(sender)>(sender), context, duration};
  };

  constexpr auto operator()(AsyncContext auto const& context,
                            Duration duration) const noexcept {
    return stdexec::__closure{*this, context, duration};
  };
};

}  // namespace async_timeout_internal

static constexpr inline auto with_timeout =
    async_timeout_internal::WithAsyncTimeout{};
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_WITH_TIMEOUT_H_
