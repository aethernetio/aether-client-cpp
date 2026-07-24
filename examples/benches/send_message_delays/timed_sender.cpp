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

#include "send_message_delays/timed_sender.h"

#include "aether/tele.h"

namespace ae::bench {
TimedSender::TimedSender(AeContext const& ae_context,
                         std::function<void(std::uint16_t id)> send_proc)
    : ae_context_{ae_context}, send_proc_{std::move(send_proc)} {
  AE_TELED_DEBUG("TimedSender");
  // send on next scheduler update
  scheduler_sub_ = ae_context_.scheduler().Task([this]() { Send(); });
}

TimedSender::ResultTimesEvent::Subscriber TimedSender::message_times_event() {
  return EventSubscriber{result_time_event_};
}

void TimedSender::Stop() {
  AE_TELED_DEBUG("Stop sending");
  result_time_event_.Emit(message_times_);
}

void TimedSender::Sync() {
  AE_TELED_DEBUG("Sync");
  Send();
}

void TimedSender::Send() {
  AE_TELED_DEBUG("Send message {} ", static_cast<int>(current_id_));

  message_times_.emplace(current_id_, HighResTimePoint::clock::now());

  send_proc_(current_id_);
  ++current_id_;
}

}  // namespace ae::bench
