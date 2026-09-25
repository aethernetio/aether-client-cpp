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

#include "send_message_delays/timed_receiver.h"

#include "aether/tele.h"

namespace ae::bench {
TimedReceiver::TimedReceiver(AeContext const& ae_context,
                             std::size_t wait_count)
    : ae_context_{ae_context},
      wait_count_{wait_count},
      result_event_{ae_context_},
      received_event_{ae_context_} {
  AE_TELED_INFO("TimedReceiver waiting {} messages", wait_count);
  last_message_time_ = Now();
  ScheduleTimeout();
}

TimedReceiver::ResultTimesEvent const& TimedReceiver::message_times_event() {
  return result_event_;
}
TimedReceiver::ReceivedEvent const& TimedReceiver::on_received() {
  return received_event_;
}

void TimedReceiver::Receive(std::uint16_t id) {
  AE_TELED_DEBUG("Message received id {}", static_cast<int>(id));

  auto [_, ok] = message_times_.emplace(id, HighResTimePoint::clock::now());
  if (!ok) {
    AE_TELED_ERROR("Duplicate message");
    return;
  }

  auto last = id >= wait_count_;
  received_event_.Emit(last);

  if (!last) {
    last_message_time_ = Now();
    ScheduleTimeout();
  } else {
    scheduler_sub_.Reset();
    result_event_.Emit(Ok{std::move(message_times_)});
  }
}

void TimedReceiver::ScheduleTimeout() {
  // on timeout
  if (!scheduler_sub_) {
    scheduler_sub_ = ae_context_.scheduler().DelayedTask(
        [this]() {
          auto current_time = Now();
          auto diff = last_message_time_ - current_time;
          if (diff > kWaitTimeout) {
            AE_TELED_ERROR("Receive message timeout");
            result_event_.Emit(Error{1});
          }
          scheduler_sub_.Reset();
        },
        kWaitTimeout);
  }
}

}  // namespace ae::bench
