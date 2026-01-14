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

#include "aether/write_action/buffer_write.h"

namespace ae {
BufferedWriteAction::BufferedWriteAction(ActionContext action_context)
    : WriteAction{action_context} {}

void BufferedWriteAction::Stop() {
  if (wa_) {
    wa_->Stop();
  } else {
    state_ = State::kStopped;
  }
}

// set to the sent state
void BufferedWriteAction::Sent(ActionPtr<WriteAction> swa) {
  wa_ = std::move(swa);
  state_ = wa_->state();
  wa_sub_ =
      wa_->state_changed().Subscribe([this](auto state) { state_ = state; });
}

// drop the write action from the buffer
void BufferedWriteAction::Drop() { state_ = State::kFailed; }
}  // namespace ae
