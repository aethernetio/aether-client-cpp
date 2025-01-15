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

#ifndef AETHER_TRANSPORT_ACTIONS_PACKET_SEND_ACTION_H_
#define AETHER_TRANSPORT_ACTIONS_PACKET_SEND_ACTION_H_

#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/state_machine.h"

namespace ae {
class PacketSendAction : public Action<PacketSendAction> {
 public:
  enum class State : std::uint8_t {
    kQueued,
    kProgress,
    kSuccess,
    kStopped,  // stopped by user
    kTimeout,  // sending timeout
    kFailed,   // failed to send
    kPanic,    // fatal unrecoverable error
  };

  PacketSendAction() = default;
  explicit PacketSendAction(ActionContext action_context)
      : Action(action_context) {}
  PacketSendAction(PacketSendAction const& other) = delete;
  PacketSendAction(PacketSendAction&& other) noexcept
      : Action(std::move(static_cast<Action&>(other))) {}

  PacketSendAction& operator=(PacketSendAction const& other) = delete;
  PacketSendAction& operator=(PacketSendAction&& other) noexcept {
    Action::operator=(std::move(static_cast<Action&>(other)));
    return *this;
  };

  virtual void Stop() = 0;
  StateMachine<State> const& state() const { return state_; }

 protected:
  StateMachine<State> state_;
};

}  // namespace ae

#endif  // AETHER_TRANSPORT_ACTIONS_PACKET_SEND_ACTION_H_
