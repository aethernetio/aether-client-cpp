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

#ifndef AETHER_ACTIONS_TIMER_ACTION_H_
#define AETHER_ACTIONS_TIMER_ACTION_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/events/event_subscription.h"

namespace ae {
class TimerAction : public Action<TimerAction> {
  enum class State : std::uint8_t {
    kStart,
    kWait,
    kTriggered,
    kStopped,
  };

 public:
  TimerAction(ActionContext action_context, Duration duration);
  TimerAction(TimerAction const& other) = delete;
  TimerAction(TimerAction&& other) noexcept;

  TimerAction& operator=(TimerAction const& other) = delete;
  TimerAction& operator=(TimerAction&& other) noexcept;

  UpdateStatus Update(TimePoint current_time);

  void Stop();

  Duration duration() const;

 private:
  Duration timer_duration_;
  TimePoint start_time_;
  StateMachine<State> state_;
};
}  // namespace ae

#endif  // AETHER_ACTIONS_TIMER_ACTION_H_
