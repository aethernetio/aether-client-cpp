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

#ifndef AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
#define AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_

#include <cstdint>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/types/state_machine.h"
#include "aether/actions/repeatable_task.h"

#include "aether/server_connections/client_to_server_stream.h"

namespace ae {
class CheckAccessForSendMessage final
    : public Action<CheckAccessForSendMessage> {
  static constexpr std::uint8_t kMaxRequestRepeatCount = 5;
  static constexpr Duration kRequestTimeout = std::chrono::seconds{1};

  enum class State : std::uint8_t {
    kSendRequest,
    kWaitResponse,
    kReceivedSuccess,
    kReceivedError,
    kSendError,
    kTimeout,
  };

 public:
  CheckAccessForSendMessage(ActionContext action_context,
                            ClientToServerStream& client_to_server_stream,
                            Uid destination);

  AE_CLASS_NO_COPY_MOVE(CheckAccessForSendMessage)

  UpdateStatus Update();

  State state() const { return state_.get(); }

 private:
  void SendRequest();
  TimePoint WaitResponse();
  void ResponseReceived();
  void ErrorReceived();
  void SendError();

  ActionContext action_context_;
  ClientToServerStream* client_to_server_stream_;
  Uid destination_;

  StateMachine<State> state_;
  ActionPtr<RepeatableTask> repeatable_task_;
  Subscription wait_check_sub_;
  Subscription state_changed_sub_;
  Subscription send_error_sub_;
  Subscription repeat_task_error_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
