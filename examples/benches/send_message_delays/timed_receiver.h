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

#include "aether/ae_context.h"
#include "aether/events/events.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {
/**
 * \brief Message receiver with fixing a message time
 * Must receive a wait_count messages during wait_timeout or emit an error
 * On success receive message_times to compare it with sent times
 */
class TimedReceiver {
  static constexpr auto kWaitTimeout = std::chrono::seconds{30};

 public:
  using ResultTimesEvent = Event<void(TimeTable const& message_times)>;
  using ReceivedEvent = Event<void(bool last)>;
  using TimeoutEvent = Event<void()>;

  TimedReceiver(AeContext const& ae_context, std::size_t wait_count);

  ResultTimesEvent::Subscriber message_times_event();
  ReceivedEvent::Subscriber on_received();
  TimeoutEvent::Subscriber on_timeout();

  void Receive(std::uint16_t id);

 private:
  void ScheduleTimeout();

  AeContext ae_context_;
  std::size_t wait_count_;

  TimeTable message_times_;
  TimePoint last_message_time_;

  ResultTimesEvent result_event_;
  ReceivedEvent received_event_;
  TimeoutEvent timeout_event_;
  TaskSubscription scheduler_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_RECEIVER_H_
