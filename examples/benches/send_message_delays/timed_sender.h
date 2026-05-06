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

#include <cstdint>
#include <functional>

#include "aether/ae_context.h"
#include "aether/events/events.h"

#include "send_message_delays/time_table.h"

namespace ae::bench {
/**
 * \brief Send message_count messages during send_duration
 * On success receive message_times to compare it with received times
 */
class TimedSender {
 public:
  using ResultTimesEvent = Event<void(TimeTable const& time_table)>;

  TimedSender(AeContext const& ae_context,
              std::function<void(std::uint16_t id)> send_proc);

  ResultTimesEvent::Subscriber message_times_event();
  void Stop();
  void Sync();

 private:
  void Send();

  AeContext ae_context_;
  std::function<void(std::uint16_t id)> send_proc_;

  std::uint16_t current_id_{0};

  TimeTable message_times_;
  ResultTimesEvent result_time_event_;
  TaskSubscription scheduler_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_TIMED_SENDER_H_
