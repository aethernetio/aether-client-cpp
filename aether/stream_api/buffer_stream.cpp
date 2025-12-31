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

#include "aether/stream_api/buffer_stream.h"

namespace ae {
BufferedWriteAction::BufferedWriteAction(ActionContext action_context)
    : StreamWriteAction(action_context) {}

void BufferedWriteAction::Stop() {
  if (swa_) {
    swa_->Stop();
  } else {
    state_ = State::kStopped;
    Action::Trigger();
  }
}

// set to the sent state
void BufferedWriteAction::Sent(ActionPtr<StreamWriteAction> swa) {
  swa_ = std::move(swa);
  state_ = swa_->state().get();
  swa_sub_ = swa_->state().changed_event().Subscribe([this](auto state) {
    state_ = state;
    Action::Trigger();
  });
}

// drop the write action from the buffer
void BufferedWriteAction::Drop() {
  state_ = State::kFailed;
  Action::Trigger();
}
}  // namespace ae
