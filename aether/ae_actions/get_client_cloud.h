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

#include <map>
#include <vector>

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/repeatable_task.h"
#include "aether/events/event_subscription.h"

#include "aether/work_cloud_api/uid_and_cloud.h"
#include "aether/work_cloud_api/server_descriptor.h"

namespace ae {
class ClientServerConnection;

class GetClientCloudAction final : public Action<GetClientCloudAction> {
  enum class State : std::uint8_t {
    kRequestCloud,
    kAllServersResolved,
    kFailed,
    kStopped,
  };

 public:
  explicit GetClientCloudAction(
      ActionContext action_context,
      ClientServerConnection& client_server_connection, Uid client_uid);

  UpdateStatus Update();

  void Stop();

  std::map<ServerId, ServerDescriptor> const& server_descriptors() const;

 private:
  void RequestCloud();
  void ResolveServers(CloudDescriptor const& cloud_descriptor);
  void RequestCloudFailed();
  void RequestServerResolve();

  void OnServerResponse(ServerDescriptor const& server_descriptor);

  ActionContext action_context_;
  ClientServerConnection* client_server_connection_;
  Uid client_uid_;

  StateMachine<State> state_;
  ActionPtr<RepeatableTask> request_cloud_task_;
  ActionPtr<RepeatableTask> server_resolve_task_;

  Subscription state_changed_subscription_;
  Subscription cloud_resolved_sub_;
  Subscription server_resolved_sub_;

  Subscription cloud_request_sub_;
  Subscription servers_resolve_sub_;

  std::vector<ServerId> requested_cloud_;
  std::map<ServerId, ServerDescriptor> server_descriptors_;
  TimePoint start_resolve_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_H_
