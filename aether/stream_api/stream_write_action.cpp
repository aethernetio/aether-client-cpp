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
FailedStreamWriteAction::FailedStreamWriteAction() {
  state_.Set(State::kFailed);
}

FailedStreamWriteAction::FailedStreamWriteAction(ActionContext action_context)
    : StreamWriteAction(action_context) {
  state_.Set(State::kFailed);
}

TimePoint FailedStreamWriteAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kFailed:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

void FailedStreamWriteAction::Stop() {}

}  // namespace ae
