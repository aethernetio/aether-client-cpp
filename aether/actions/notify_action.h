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

#ifndef AETHER_ACTIONS_NOTIFY_ACTION_H_
#define AETHER_ACTIONS_NOTIFY_ACTION_H_

#include "aether/actions/action.h"

namespace ae {
class NotifyAction : public Action<NotifyAction> {
 public:
  using Action<NotifyAction>::Action;
  using Action<NotifyAction>::operator=;

  UpdateStatus Update() const {
    if (notify_success_) {
      return UpdateStatus::Result();
    }
    if (notify_failed_) {
      return UpdateStatus::Error();
    }
    if (notify_stopped_) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  void Stop() {
    notify_stopped_ = true;
    this->Trigger();
  }

  void Notify() {
    notify_success_ = true;
    this->Trigger();
  }

  void Failed() {
    notify_failed_ = true;
    this->Trigger();
  }

 private:
  bool notify_success_{};
  bool notify_failed_{};
  bool notify_stopped_{};
};
}  // namespace ae

#endif  // AETHER_ACTIONS_NOTIFY_ACTION_H_
