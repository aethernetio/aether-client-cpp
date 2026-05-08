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

#include "send_messages_bandwidth/receiver/message_receiver.h"

#include "aether/tele/tele.h"

namespace ae::bench {
MessageReceiver::MessageReceiver(AeContext const& ae_context)
    : ae_context_{ae_context} {
  first_message_received_time_ = HighResTimePoint::clock::now();
  received_message_time_ = Now();
  SchedulerReceiveTimeout();
}

MessageReceiver::ResultEvent::Subscriber MessageReceiver::result_event() {
  return EventSubscriber{result_event_};
}

void MessageReceiver::MessageReceived(std::uint16_t id) {
  AE_TELED_DEBUG("Message received {} count {}", id, message_received_count_);
  ++message_received_count_;
  received_message_time_ = Now();
}

void MessageReceiver::StopTest() {
  wait_sub_.Reset();
  EmitResult();
}

void MessageReceiver::Stop() {
  wait_sub_.Reset();
  result_event_.Emit(Error{1});
}

void MessageReceiver::SchedulerReceiveTimeout() {
  if (!wait_sub_) {
    wait_sub_ = ae_context_.scheduler().DelayedTask(
        [&]() {
          auto diff = Now() - received_message_time_;
          if (diff >= kReceiveTimeout) {
            AE_TELED_ERROR("Message receive timeout");
            result_event_.Emit(Error{12});
          } else {
            wait_sub_.Reset();
            SchedulerReceiveTimeout();
          }
        },
        kReceiveTimeout);
  }
}

void MessageReceiver::EmitResult() {
  auto last_message_received_time = HighResTimePoint::clock::now();
  result_event_.Emit(Ok{
      RecvResult{
          .count = message_received_count_,
          .duration =
              (last_message_received_time - first_message_received_time_),
      },
  });
}

}  // namespace ae::bench
