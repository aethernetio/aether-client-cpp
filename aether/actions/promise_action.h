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

#ifndef AETHER_ACTIONS_PROMISE_ACTION_H_
#define AETHER_ACTIONS_PROMISE_ACTION_H_

#include <cstdint>
#include <optional>

#include "aether/actions/action.h"
#include "aether/types/state_machine.h"

namespace ae {
template <typename TValue>
class PromiseAction : public Action<PromiseAction<TValue>> {
  enum class State : std::uint8_t {
    kEmpty,
    kValue,
    kError,
  };

  using BaseAction = Action<PromiseAction<TValue>>;

 public:
  using BaseAction::BaseAction;

  AE_CLASS_MOVE_ONLY(PromiseAction);

  UpdateStatus Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kEmpty:
          break;
        case State::kValue:
          return UpdateStatus::Result();
          break;
        case State::kError:
          return UpdateStatus::Error();
          break;
      }
    }
    return {};
  }

  void SetValue(TValue value) {
    value_ = std::move(value);
    state_ = State::kValue;
    BaseAction::Trigger();
  }

  void Reject() {
    state_ = State::kError;
    BaseAction::Trigger();
  }

  TValue& value() {
    assert(value_);
    return *value_;
  }

  TValue const& value() const {
    assert(value_);
    return *value_;
  }

 private:
  std::optional<TValue> value_{};
  StateMachine<State> state_{State::kEmpty};
};

template <>
class PromiseAction<void> : public Action<PromiseAction<void>> {
  enum class State : std::uint8_t {
    kEmpty,
    kValue,
    kError,
  };

  using BaseAction = Action<PromiseAction<void>>;

 public:
  using BaseAction::BaseAction;

  AE_CLASS_MOVE_ONLY(PromiseAction);

  UpdateStatus Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kEmpty:
          break;
        case State::kValue:
          return UpdateStatus::Result();
          break;
        case State::kError:
          return UpdateStatus::Error();
          break;
      }
    }
    return {};
  }

  void SetValue() {
    state_ = State::kValue;
    BaseAction::Trigger();
  }

  void Reject() {
    state_ = State::kError;
    BaseAction::Trigger();
  }

 private:
  StateMachine<State> state_{State::kEmpty};
};
}  // namespace ae
#endif  // AETHER_ACTIONS_PROMISE_ACTION_H_
