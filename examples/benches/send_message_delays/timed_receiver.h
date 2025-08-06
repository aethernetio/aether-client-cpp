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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_

#include <cstddef>

#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {
/**
 * \brief Message receiver with fixing a message time
 * Must receive a wait_count messages during wait_timeout or emit an error
 * On success receive message_times to compare it with sent times
 */
class TimedReceiver : public Action<TimedReceiver> {
  enum class State : std::uint8_t {
    kWaiting,
    kReceived,
    kTimeOut,
  };

  static constexpr auto kWaitTimeout = std::chrono::seconds{30};

 public:
  TimedReceiver(ActionContext action_context, std::size_t wait_count);

  ActionResult Update();

  TimeTable const& message_times() const { return message_times_; }

  void Receive(std::uint16_t id);

  EventSubscriber<void(bool last)> OnReceived() {
    return EventSubscriber{received_event_};
  }

 private:
  ActionResult CheckTimeout(TimePoint current_time);

  std::size_t wait_count_;

  TimePoint next_receive_time_;
  TimeTable message_times_;

  Event<void(bool last)> received_event_;
  StateMachine<State> state_;
  Subscription state_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_
