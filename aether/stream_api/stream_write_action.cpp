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

#include "aether/stream_api/stream_write_action.h"

namespace ae {

StreamWriteAction::StreamWriteAction(ActionContext action_context)
    : Action{action_context}, state_{State::kQueued} {}

StreamWriteAction::StreamWriteAction(StreamWriteAction&& other) noexcept
    : Action{std::move(other)}, state_{std::move(other.state_)} {}

StreamWriteAction& StreamWriteAction::operator=(
    StreamWriteAction&& other) noexcept {
  if (this != &other) {
    Action::operator=(std::move(other));
    state_ = std::move(other.state_);
  }
  return *this;
}

UpdateStatus StreamWriteAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kDone:
        return UpdateStatus::Result();
      case State::kStopped:
        return UpdateStatus::Stop();
      case State::kFailed:
        return UpdateStatus::Error();
      default:
        break;
    }
  }

  return {};
}

void StreamWriteAction::Stop() {
  state_ = State::kStopped;
  Action::Trigger();
}

FailedStreamWriteAction::FailedStreamWriteAction(ActionContext action_context)
    : StreamWriteAction{action_context} {
  state_.Set(State::kFailed);
}

UpdateStatus FailedStreamWriteAction::Update() {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kFailed:
        return UpdateStatus::Error();
      default:
        break;
    }
  }
  return {};
}

void FailedStreamWriteAction::Stop() {}

}  // namespace ae
