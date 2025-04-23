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

#ifndef AETHER_API_PROTOCOL_PROMISE_ACTION_H_
#define AETHER_API_PROTOCOL_PROMISE_ACTION_H_

#include <cstdint>
#include <optional>

#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_context.h"

#include "aether/api_protocol/request_id.h"

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
  PromiseAction(ActionContext action_context, RequestId req_id)
      : BaseAction{action_context}, request_id_{req_id} {}

  PromiseAction(PromiseAction const& other) = delete;

  PromiseAction(PromiseAction&& other) noexcept
      : BaseAction{std::move(other)}, request_id_{other.request_id_} {}

  PromiseAction& operator=(PromiseAction const& other) = delete;
  PromiseAction& operator=(PromiseAction&& other) = delete;

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kEmpty:
          break;
        case State::kValue:
          BaseAction::Result(*this);
          break;
        case State::kError:
          BaseAction::Error(*this);
          break;
      }
    }
    return current_time;
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

  RequestId const& request_id() const { return request_id_; }

 private:
  RequestId request_id_;
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
  PromiseAction(ActionContext action_context, RequestId req_id)
      : BaseAction{action_context}, request_id_{req_id} {}

  PromiseAction(PromiseAction const& other) = delete;

  PromiseAction(PromiseAction&& other) noexcept
      : BaseAction{std::move(other)}, request_id_{other.request_id_} {}

  PromiseAction& operator=(PromiseAction const& other) = delete;
  PromiseAction& operator=(PromiseAction&& other) = delete;

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kEmpty:
          break;
        case State::kValue:
          BaseAction::Result(*this);
          break;
        case State::kError:
          BaseAction::Error(*this);
          break;
      }
    }
    return current_time;
  }

  void SetValue() {
    state_ = State::kValue;
    BaseAction::Trigger();
  }

  void Reject() {
    state_ = State::kError;
    BaseAction::Trigger();
  }

  RequestId const& request_id() const { return request_id_; }

 private:
  RequestId request_id_;
  StateMachine<State> state_{State::kEmpty};
};

template <typename T>
using PromiseView = ActionView<PromiseAction<T>>;
}  // namespace ae
#endif  // AETHER_API_PROTOCOL_PROMISE_ACTION_H_
