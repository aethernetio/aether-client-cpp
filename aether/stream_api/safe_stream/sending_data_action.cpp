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
OffsetRange SendingData::get_offset_range(
    SafeStreamRingIndex::type window_size) const {
  return OffsetRange{
      offset, offset + static_cast<SafeStreamRingIndex::type>(data.size() - 1),
      window_size};
}

SendingDataAction::SendingDataAction(ActionContext action_context,
                                     SendingData sending_data)
    : Action{action_context},
      sending_data_{std::move(sending_data)},
      state_{State::kWaiting} {}

TimePoint SendingDataAction::Update(TimePoint current_time) {
  if (!state_.changed()) {
    return current_time;
  }
  switch (state_.Acquire()) {
    case State::kWaiting:
    case State::kSending:
      break;
    case State::kDone:
      Action::Result(*this);
      break;
    case State::kStopped:
      Action::Stop(*this);
      break;
    case State::kFailed:
      Action::Error(*this);
      break;
  }

  return current_time;
}

SendingData& SendingDataAction::sending_data() { return sending_data_; }

EventSubscriber<void()> SendingDataAction::stop_event() { return stop_event_; }

void SendingDataAction::Sending() { state_ = State::kSending; }

void SendingDataAction::Stop() {
  if (state_ == State::kSending) {
    AE_TELED_ERROR("Unable to stop sending data action while sending");
    return;
  }
  stop_event_.Emit();
}

void SendingDataAction::SentConfirmed() {
  state_ = State::kDone;
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
}  // namespace ae
