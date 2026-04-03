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

#include <cassert>

namespace ae {
BufferedWriteAction::BufferedWriteAction() noexcept = default;

void BufferedWriteAction::Stop() noexcept {
  if (wa_sub_) {  // wa_sub_ is used as alive indicator
    assert(wa_ != nullptr && "Child write action is null");
    // send stop request and wait for status event
    wa_->Stop();
  } else {
    SetStatus(ae::WriteAction::Status::kStop);
  }
}

// set to the sent state
void BufferedWriteAction::Sent(WriteAction& swa) {
  wa_ = &swa;
  wa_sub_ =
      wa_->status_event().Subscribe([this](auto status) { SetStatus(status); });
}

// drop the write action from the buffer
void BufferedWriteAction::Drop() {
  wa_sub_.Reset();
  SetStatus(WriteAction::Status::kFail);
}
}  // namespace ae
