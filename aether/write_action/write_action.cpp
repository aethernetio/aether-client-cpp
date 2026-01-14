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

#include "aether/write_action/write_action.h"

namespace ae {

WriteAction::WriteAction(ActionContext action_context)
    : Action{action_context}, state_{State::kQueued} {
  state_.changed_event().Subscribe([this](auto state) {
    Action::Trigger();
    state_changed_.Emit(state);
  });
}

UpdateStatus WriteAction::Update() {
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

void WriteAction::Stop() { state_ = State::kStopped; }
}  // namespace ae
