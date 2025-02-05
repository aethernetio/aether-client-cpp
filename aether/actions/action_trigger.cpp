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

#include <mutex>

namespace ae {
ActionTrigger::ActionTrigger() : sync_object_{MakeRcPtr<SyncObject>()} {}

void ActionTrigger::Wait() {
  bool res = true;
  sync_object_->triggered.compare_exchange_strong(res, false);
  if (res) {
    return;
  }
  std::unique_lock<std::mutex> lock(sync_object_->mutex);
  sync_object_->condition.wait(lock, [&]() {
    sync_object_->triggered.compare_exchange_strong(res, false);
    return res;
  });
  sync_object_->triggered = false;
}

bool ActionTrigger::WaitUntil(TimePoint timeout) {
  bool res = true;
  sync_object_->triggered.compare_exchange_strong(res, false);
  if (res) {
    return true;
  }
  std::unique_lock<std::mutex> lock(sync_object_->mutex);
  auto result = sync_object_->condition.wait_until(lock, timeout, [&]() {
    sync_object_->triggered.compare_exchange_strong(res, false);
    return res;
  });
  sync_object_->triggered = false;
  return result;
}

void ActionTrigger::Trigger() {
  std::lock_guard lock(sync_object_->mutex);
  sync_object_->triggered = true;
  sync_object_->condition.notify_all();
}

void Merge(ActionTrigger& left, ActionTrigger& right) {
  left.sync_object_.Reset();
  // make it the same object
  left.sync_object_ = right.sync_object_;
}
}  // namespace ae
