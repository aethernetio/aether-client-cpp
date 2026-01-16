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

#ifndef AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_H_
#define AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_H_

#include <vector>

#include "aether/types/uid.h"
#include "aether/actions/action.h"
#include "aether/types/server_id.h"
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class GetClientCloudAction final : public Action<GetClientCloudAction> {
  enum class State : std::uint8_t {
    kNone,
    kResult,
    kFailed,
    kStopped,
  };

 public:
  GetClientCloudAction(ActionContext action_context, Uid client_uid,
                       CloudServerConnections& cloud_connection,
                       RequestPolicy::Variant request_policy);

  UpdateStatus Update();

  void Stop();

  std::vector<ServerId> const& cloud() const;

 private:
  Uid client_uid_;
  StateMachine<State> state_;
  OwnActionPtr<CloudRequestAction> cloud_request_;
  Subscription cloud_request_sub_;

  std::vector<ServerId> cloud_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_H_
