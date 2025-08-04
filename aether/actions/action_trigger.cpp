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

#include "aether/actions/action_trigger.h"

namespace ae {
ActionTrigger::ActionTrigger() : sync_object_{MakeRcPtr<SyncObject>()} {}

void ActionTrigger::Wait() {
  if (IsTriggered()) {
    return;
  }
  std::unique_lock<std::mutex> lock(sync_object_->mutex);
  sync_object_->condition.wait(lock, [&]() { return IsTriggered(); });
}

bool ActionTrigger::WaitUntil(TimePoint timeout) {
  if (IsTriggered()) {
    return true;
  }
  std::unique_lock<std::mutex> lock(sync_object_->mutex);
  auto result = sync_object_->condition.wait_until(
      lock, timeout, [&]() { return IsTriggered(); });
  return result;
}

void ActionTrigger::Trigger() {
  std::lock_guard lock(sync_object_->mutex);
  sync_object_->triggered = true;
  sync_object_->condition.notify_all();
}

bool ActionTrigger::IsTriggered() const {
  bool res = true;
  sync_object_->triggered.compare_exchange_strong(res, false);
  return res;
}

void Merge(ActionTrigger& left, ActionTrigger& right) {
  left.sync_object_.Reset();
  // make it the same object
  left.sync_object_ = right.sync_object_;
}
}  // namespace ae
