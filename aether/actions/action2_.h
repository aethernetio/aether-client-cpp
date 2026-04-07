/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_ACTIONS_ACTION2_H_
#define AETHER_ACTIONS_ACTION2_H_

#include "aether/common.h"
#include "aether/events/events.h"

namespace ae::a2 {
class Action {
 public:
  using FinishedEvent = Event<void()>;

  Action() noexcept = default;
  virtual ~Action() noexcept = default;

  AE_CLASS_MOVE_ONLY(Action)

  void Finish() noexcept {
    is_finished_ = true;
    finished_event_.Emit();
  }

  FinishedEvent::Subscriber finished_event() noexcept {
    return EventSubscriber{finished_event_};
  }

  bool is_finished() const noexcept { return is_finished_; }

 private:
  bool is_finished_ = false;
  FinishedEvent finished_event_;
};
}  // namespace ae::a2

#endif  // AETHER_ACTIONS_ACTION2_H_
