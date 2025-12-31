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
#include "aether/types/uid.h"
#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/types/state_machine.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class CloudServerConnections;
class CheckAccessForSendMessage final
    : public Action<CheckAccessForSendMessage> {
  enum class State : std::uint8_t {
    kNone,
    kReceivedSuccess,
    kReceivedError,
    kSendError,
  };

 public:
  CheckAccessForSendMessage(ActionContext action_context, Uid destination,
                            CloudServerConnections& cloud_connection,
                            RequestPolicy::Variant request_policy);

  AE_CLASS_NO_COPY_MOVE(CheckAccessForSendMessage)

  UpdateStatus Update();

  State state() const { return state_.get(); }

 private:
  void ResponseReceived();
  void ErrorReceived();

  Uid destination_;

  StateMachine<State> state_;
  OwnActionPtr<CloudRequestAction> cloud_request_;
  Subscription wait_check_sub_;
  Subscription request_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_CHECK_ACCESS_FOR_SEND_MESSAGE_H_
