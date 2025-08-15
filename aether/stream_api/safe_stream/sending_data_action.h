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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_
#define AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_

#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae {
class SendingDataAction : public Action<SendingDataAction> {
  enum class State : std::uint8_t {
    kWaiting,
    kSending,
    kDone,
    kStopped,
    kFailed,
  };

 public:
  using Action::Action;

  SendingDataAction(ActionContext action_context, SendingData data);

  UpdateStatus Update();

  SendingData const& sending_data() const;
  // The stop event is emitted by stop command.
  EventSubscriber<void(SendingData const&)> stop_event();

  // command to stop the sending action
  void Stop();

  /**
   * \brief Acknowledge part of the data till offset as acknowledged.
   * If the whole sending_data_ is acknowledged, the action is marked as done.
   * \param offset The offset to acknowledge.
   * \return true if the whole sending_data_ is acknowledged.
   */
  bool Acknowledge(SSRingIndex offset);

  // mark action as sending
  void Sending();
  // mark action as stopped
  void Stopped();
  // mark action as failed
  void Failed();

  /**
   * \brief Update the offset of sending_data_ to new  value.
   * \param offset The new offset.
   * \return Old value.
   */
  SSRingIndex UpdateOffset(SSRingIndex offset);

 private:
  SendingData sending_data_;
  StateMachine<State> state_;
  Event<void(SendingData const&)> stop_event_;
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SENDING_DATA_ACTION_H_
