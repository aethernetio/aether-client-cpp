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
template <typename T = void>
class NotifyAction : public Action<NotifyAction<T>> {
 public:
  using Action<NotifyAction<T>>::Action;
  using Action<NotifyAction<T>>::operator=;

  TimePoint Update(TimePoint current_time) override {
    if (notify_success_) {
      notify_success_ = false;
      this->ResultRepeat(static_cast<T&>(*this));
    }
    if (notify_failed_) {
      notify_failed_ = false;
      this->Error(static_cast<T&>(*this));
    }
    return current_time;
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
};

template <>
class NotifyAction<void> : public Action<NotifyAction<void>> {
 public:
  using Action<NotifyAction<void>>::Action;
  using Action<NotifyAction<void>>::operator=;

  TimePoint Update(TimePoint current_time) override {
    if (notify_success_) {
      notify_success_ = false;
      this->ResultRepeat(*this);
    }
    if (notify_failed_) {
      notify_failed_ = false;
      this->Error(*this);
    }
    return current_time;
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
};
}  // namespace ae

#endif  // AETHER_ACTIONS_NOTIFY_ACTION_H_
