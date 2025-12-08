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

#ifndef AETHER_AE_ACTIONS_GET_SERVERS_H_
#define AETHER_AE_ACTIONS_GET_SERVERS_H_

#include <vector>

#include "aether/actions/action.h"
#include "aether/types/server_id.h"
#include "aether/types/state_machine.h"
#include "aether/actions/repeatable_task.h"
#include "aether/client_connections/cloud_connection.h"

namespace ae {
class GetServersAction : public Action<GetServersAction> {
  enum class State : std::uint8_t {
    kMakeRequest,
    kWait,
    kResult,
    kFailed,
    kStop,
  };

 public:
  GetServersAction(ActionContext action_context,
                   std::vector<ServerId> server_ids,
                   CloudConnection& cloud_connection,
                   RequestPolicy::Variant request_policy);

  UpdateStatus Update(TimePoint current_time);

  std::vector<ServerDescriptor> const& servers() const;
  void Stop();

 private:
  void MakeRequest();
  void GetResponse(ServerDescriptor const& server_descriptor);
  UpdateStatus Wait(TimePoint current_time);

  ActionContext action_context_;
  std::vector<ServerId> server_ids_;
  CloudConnection* cloud_connection_;
  RequestPolicy::Variant request_policy_;
  CloudConnection::ReplicaSubscription response_sub_;
  StateMachine<State> state_;
  TimePoint timeout_point_;

  OwnActionPtr<RepeatableTask> repeatable_task_;
  Subscription request_sub_;

  std::vector<ServerDescriptor> server_descriptors_;
};
}  // namespace ae
#endif  // AETHER_AE_ACTIONS_GET_SERVERS_H_
