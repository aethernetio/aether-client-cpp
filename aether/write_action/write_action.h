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

#ifndef AETHER_WRITE_ACTION_WRITE_ACTION_H_
#define AETHER_WRITE_ACTION_WRITE_ACTION_H_

#include "aether/events/events.h"
#include "aether/actions/action.h"

namespace ae {
/**
 * \brief Base type for write action.
 */
class WriteAction : public Action {
 public:
  enum class Status : unsigned char {
    kSuccess,
    kFail,
    kStop,
  };
  using StatusEvent = Event<void(Status)>;

  WriteAction() noexcept = default;
  ~WriteAction() noexcept override = default;

  AE_CLASS_MOVE_ONLY(WriteAction)

  /**
   * \brief Stop the writing action
   */
  virtual void Stop() noexcept {}

  /**
   * \brief Subscribe to status event of this action.
   */
  virtual StatusEvent::Subscriber status_event() noexcept {
    return EventSubscriber{status_event_};
  }

 protected:
  virtual void SetStatus(Status status) noexcept {
    status_event_.Emit(status);
    Finish();
  }
  StatusEvent status_event_;
};

}  // namespace ae
#endif  // AETHER_WRITE_ACTION_WRITE_ACTION_H_
