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

#include "aether/executors/async_context.h"
#include "aether/tasks/details/task_subsctiption.h"

namespace ae::ex {
struct TimeoutError {};

namespace async_timeout_internal {
template <typename R>
struct OpBase {
  constexpr explicit OpBase(R&& recv) noexcept : recv{std::move(recv)} {}

  void Reset() noexcept {
    reset_flag = true;
    task_sub.Reset();
  }
  bool is_reset() const noexcept { return reset_flag; }

  R recv;
  bool reset_flag{false};
  TaskSubscription task_sub;
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

template <AsyncContext AC, stdexec::sender Child, stdexec::receiver R>
class Operation {
 public:
  using local_receiver = Receiver<R>;
  using op_state = stdexec::connect_result_t<Child, local_receiver>;

  constexpr Operation(AC const& ac, Child&& child,
                      std::chrono::milliseconds duration, R&& recv)
      : ac_{ac},
        op_base_{std::move(recv)},
        duration_{duration},
        op_{stdexec::connect(std::move(child),
                             local_receiver{.op = &op_base_})} {}

  constexpr void start() noexcept {
    // if delayed task executed set timeout error
    // task_sub controls if task reset or no
    op_base_.task_sub = ac_.scheduler().DelayedTask(
        [&]() noexcept {
          stdexec::set_error(std::move(op_base_.recv), TimeoutError{});
          op_base_.Reset();
        },
        duration_);

    op_.start();
  }

 private:
  AC ac_;
  OpBase<R> op_base_;
  std::chrono::milliseconds duration_;
  op_state op_;
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

  template <typename Duration>
  constexpr Sender(Child&& child, AC const& ac, Duration duration) noexcept
      : child_{std::move(child)},
        ac_{ac},
        duration_{
            std::chrono::duration_cast<std::chrono::milliseconds>(duration)} {}

  template <stdexec::receiver R>
  constexpr auto connect(R&& recv) && noexcept {
    return Operation<AC, Child, R>{
        ac_,
        std::move(child_),
        duration_,
        std::forward<R>(recv),
    };
  }

 private:
  Child child_;
  AC ac_;
  std::chrono::milliseconds duration_;
};

struct WithAsyncTimeout {
  constexpr auto operator()(stdexec::sender auto&& sender,
                            AsyncContext auto const& context,
                            auto duration) const noexcept {
    return Sender{std::forward<decltype(sender)>(sender), context, duration};
  }

  constexpr auto operator()(AsyncContext auto const& context,
                            auto duration) const noexcept {
    return stdexec::__closure{*this, context, duration};
  }
};

}  // namespace async_timeout_internal

static constexpr inline auto with_timeout =
    async_timeout_internal::WithAsyncTimeout{};
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_WITH_TIMEOUT_H_
