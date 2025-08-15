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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_

#include <cstddef>
#include <functional>

#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {
/**
 * \brief Send message_count messages during send_duration
 * On success receive message_times to compare it with received times
 */
class TimedSender : public Action<TimedSender> {
  enum class State : std::uint8_t {
    kSend,
    kWaitSync,
    kFinished,
    kError,
  };

 public:
  TimedSender(ActionContext action_context,
              std::function<void(std::uint16_t id)> send_proc,
              Duration send_interval);

  UpdateStatus Update();

  TimeTable const& message_times() const { return message_times_; }

  void Stop();
  void Sync();

 private:
  void Send();
  UpdateStatus CheckSyncTimeout(TimePoint current_time);

  std::function<void(std::uint16_t id)> send_proc_;

  Duration send_interval_;
  std::uint16_t current_id_{0};

  TimePoint next_send_time_;

  TimeTable message_times_;
  StateMachine<State> state_;
  Subscription state_changed_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_
