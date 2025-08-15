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

#include "aether/stream_api/safe_stream/sending_data_action.h"

#include "aether/tele/tele.h"

namespace ae {
SendingDataAction::SendingDataAction(ActionContext action_context,
                                     SendingData sending_data)
    : Action{action_context},
      sending_data_{std::move(sending_data)},
      state_{State::kWaiting} {}

UpdateStatus SendingDataAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaiting:
      case State::kSending:
        break;
      case State::kDone:
        return UpdateStatus::Result();
      case State::kStopped:
        return UpdateStatus::Stop();
      case State::kFailed:
        return UpdateStatus::Error();
    }
  }
  return {};
}

SendingData const& SendingDataAction::sending_data() const {
  return sending_data_;
}

EventSubscriber<void(SendingData const&)> SendingDataAction::stop_event() {
  return EventSubscriber{stop_event_};
}

void SendingDataAction::Stop() {
  if (state_ == State::kSending) {
    AE_TELED_ERROR("Unable to stop sending data action while sending");
    return;
  }
  stop_event_.Emit(sending_data_);
}

bool SendingDataAction::Acknowledge(SSRingIndex offset) {
  assert(sending_data_.offset.IsBefore(offset));

  auto distance = sending_data_.offset.Distance(offset);
  auto size = sending_data_.end - sending_data_.begin;

  sending_data_.begin =
      size > distance ? sending_data_.begin + distance : sending_data_.end;

  if (sending_data_.begin == sending_data_.end) {
    state_ = State::kDone;
    Action::Trigger();
    return true;
  }
  sending_data_.offset = offset;

  return false;
}

void SendingDataAction::Sending() {
  state_ = State::kSending;
  Action::Trigger();
}

void SendingDataAction::Stopped() {
  state_ = State::kStopped;
  Action::Trigger();
}

void SendingDataAction::Failed() {
  state_ = State::kFailed;
  Action::Trigger();
}

SSRingIndex SendingDataAction::UpdateOffset(SSRingIndex offset) {
  std::swap(sending_data_.offset, offset);
  return offset;
}
}  // namespace ae
