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

#ifndef AETHER_STREAM_API_STREAM_WRITE_ACTION_H_
#define AETHER_STREAM_API_STREAM_WRITE_ACTION_H_

#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

namespace ae {
class StreamWriteAction : public Action<StreamWriteAction> {
 public:
  enum class State : std::uint8_t {
    kQueued,
    kInProgress,
    kDone,
    kTimeout,  // write timeout
    kStopped,  // stopped by user
    kFailed,   // failed to send
    kPanic,    // fatal unrecoverable error
  };

  using Action::Action;

  /**
   * \brief Stop the writing action
   */
  virtual void Stop() = 0;

  StateMachine<State> const& state() const { return state_; }

 protected:
  StateMachine<State> state_{State::kQueued};
};

class FailedStreamWriteAction final : public StreamWriteAction {
 public:
  using StreamWriteAction::StreamWriteAction;
  FailedStreamWriteAction();
  explicit FailedStreamWriteAction(ActionContext action_context);

  TimePoint Update(TimePoint current_time) override;
  void Stop() override;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_STREAM_WRITE_ACTION_H_
