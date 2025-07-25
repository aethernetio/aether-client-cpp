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

#ifndef AETHER_ACTIONS_ACTION_TRIGGER_H_
#define AETHER_ACTIONS_ACTION_TRIGGER_H_

#include <mutex>
#include <atomic>
#include <condition_variable>

#include "aether/common.h"
#include "aether/ptr/rc_ptr.h"

namespace ae {
struct SyncObject {
  std::mutex mutex;
  std::condition_variable condition;
  std::atomic<bool> triggered{false};
};

class ActionTrigger {
  friend void Merge(ActionTrigger& left, ActionTrigger& right);

 public:
  ActionTrigger();
  ~ActionTrigger() = default;

  // Wait trigger
  void Wait();
  // Return false on timeout
  bool WaitUntil(TimePoint timeout);

  // call this by action if update required
  void Trigger();

  bool IsTriggered() const;

 private:
  RcPtr<SyncObject> sync_object_;
};

// merge to action triggers in one
extern void Merge(ActionTrigger& left, ActionTrigger& right);

}  // namespace ae

#endif  // AETHER_ACTIONS_ACTION_TRIGGER_H_
