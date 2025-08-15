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
MessageReceiver::MessageReceiver(ActionContext action_context)
    : Action{action_context}, state_{State::kReceiving} {}

UpdateStatus MessageReceiver::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kReceiving:
        receive_message_timeout_ = kReceiveTimeout + Now();
        first_message_received_time_ = HighResTimePoint::clock::now();
        break;
      case State::kSuccess:
        return UpdateStatus::Result();
      case State::kError:
        return UpdateStatus::Error();
      case State::kStopped:
        return UpdateStatus::Stop();
    }
  }
  if (state_.get() == State::kReceiving) {
    return CheckReceiveTimeout(Now());
  }
  return {};
}

std::size_t MessageReceiver::message_received_count() const {
  return message_received_count_;
}

Duration MessageReceiver::receive_duration() const {
  return std::chrono::duration_cast<Duration>(last_message_received_time_ -
                                              first_message_received_time_);
}

void MessageReceiver::MessageReceived(std::uint16_t id) {
  AE_TELED_DEBUG("Message received {} count {}", id, message_received_count_);
  last_message_received_time_ = HighResTimePoint::clock::now();
  receive_message_timeout_ = kReceiveTimeout + Now();
  ++message_received_count_;
  received_message_ids_.insert(id);
}

void MessageReceiver::StopTest() {
  state_ = State::kSuccess;
  Action::Trigger();
}

void MessageReceiver::Stop() {
  state_ = State::kStopped;
  Action::Trigger();
}

UpdateStatus MessageReceiver::CheckReceiveTimeout(TimePoint current_time) {
  if (receive_message_timeout_ > current_time) {
    return UpdateStatus::Delay(receive_message_timeout_);
  }
  AE_TELED_ERROR("Message receive timeout");
  state_ = State::kError;
  Action::Trigger();
  return {};
}

}  // namespace ae::bench
