/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_TYPES_STATE_MACHINE_H_
#define AETHER_TYPES_STATE_MACHINE_H_

#include <type_traits>

#include "aether/events/events.h"

namespace ae {

template <typename TEnum>
class StateMachine {
 public:
  static_assert(std::is_enum_v<TEnum> || std::is_integral_v<TEnum>,
                "Expected TEnum to be an enum or integral type");

  explicit StateMachine(TEnum val) : changed_{true}, state_(val) {}

  StateMachine(StateMachine const& other) = delete;
  StateMachine(StateMachine&& other) noexcept = default;

  StateMachine& operator=(StateMachine const&) = delete;
  StateMachine& operator=(StateMachine&&) = default;

  bool changed() const { return changed_; }

  /**
   * \brief Get current state value and mark it unchanged
   */
  TEnum Acquire() {
    changed_ = false;
    return state_;
  }

  TEnum get() const { return state_; }
  operator TEnum() const { return state_; }

  /**
   * \brief Set new state value and mark it changed
   */
  void Set(TEnum val) {
    if (state_ != val) {
      state_ = val;
      changed_ = true;
      changed_event_.Emit(state_);
    }
  }

  StateMachine& operator=(TEnum val) {
    Set(val);
    return *this;
  }

  /**
   * \brief Set new state value but not mark it changed
   */
  void Assign(TEnum val) { state_ = val; }

  /**
   * \brief Event that is called when state is changed
   */
  EventSubscriber<void(TEnum)> changed_event() const {
    return EventSubscriber{changed_event_};
  }

 private:
  bool changed_;
  TEnum state_;
  mutable Event<void(TEnum)> changed_event_;
};
}  // namespace ae

#endif  // AETHER_TYPES_STATE_MACHINE_H_
