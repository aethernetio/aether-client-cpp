/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_EXECUTORS_WAIT_RESULT_H_
#define AETHER_EXECUTORS_WAIT_RESULT_H_

#include <optional>

#include "third_party/libunifex/include/unifex/sender_concepts.hpp"

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

namespace ae::ex {
namespace wait_result_internal {
template <typename Sender, typename Result>
class WaitResultAction final : public Action<WaitResultAction<Sender, Result>> {
  using Base = Action<WaitResultAction<Sender, Result>>;

  enum class State : char {
    kInProgress,
    kValue,
    kError,
    kDone,
  };

  class Receiver {
   public:
    template <typename... Values>
    void set_value(Values&&... vs) && noexcept {
      wait_res_action->value_.emplace(std::forward<Values>(vs)...);
      wait_res_action->state_ = State::kValue;
      wait_res_action->Complete();
    }

    template <typename Errr>
    void set_error(Errr&&) && noexcept {
      wait_res_action->state_ = State::kError;
      wait_res_action->Complete();
    }

    void set_done() && noexcept {
      wait_res_action->state_ = State::kDone;
      wait_res_action->Complete();
    }

    WaitResultAction* wait_res_action;
  };

  using OperationState = decltype(unifex::connect(std::declval<Sender&&>(),
                                                  std::declval<Receiver&&>()));

 public:
  WaitResultAction(ActionContext action_context, Sender&& sender)
      : Base{action_context},
        sender_{std::move(sender)},
        op_{unifex::connect(std::move(sender_), Receiver{this})},
        state_{State::kInProgress} {}

  UpdateStatus Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kInProgress:
          unifex::start(op_);
          break;
        case State::kValue:
          return UpdateStatus::Result();
        case State::kError:
          return UpdateStatus::Error();
        case State::kDone:
          return UpdateStatus::Stop();
      }
    }

    return {};
  }

  std::optional<Result>& result() { return value_; }

 private:
  void Complete() { Base::Trigger(); }

  Sender sender_;
  OperationState op_;
  std::optional<Result> value_;
  StateMachine<State> state_;
};
}  // namespace wait_result_internal

template <typename Sender>
auto WaitResult(ActionContext action_context, Sender&& sender) {
  return ActionPtr<wait_result_internal::WaitResultAction<
      std::decay_t<Sender>,
      unifex::sender_single_value_result_t<std::decay_t<Sender>>>>{
      action_context, std::forward<Sender>(sender)};
}
}  // namespace ae::ex

#endif  // AETHER_EXECUTORS_WAIT_RESULT_H_
