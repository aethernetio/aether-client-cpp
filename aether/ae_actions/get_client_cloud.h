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

#include "aether/memory.h"
#include "aether/actions/action.h"
#include "aether/events/multi_subscription.h"

#include "aether/stream_api/stream_api.h"
#include "aether/stream_api/header_gate.h"
#include "aether/stream_api/gates_stream.h"
#include "aether/stream_api/serialize_gate.h"

#include "aether/methods/uid_and_cloud.h"
#include "aether/methods/server_descriptor.h"

#include "aether/client_connections/client_to_server_stream.h"

namespace ae {
class GetClientCloudAction : public Action<GetClientCloudAction> {
  enum class State : std::uint8_t {
    kRequestCloud,
    kRequestServerResolve,
    kAllServersResolved,
    kFailed,
    kStopped,
  };

 public:
  explicit GetClientCloudAction(ActionContext action_context,
                                ClientToServerStream& client_to_server_stream,
                                Uid client_uid);

  TimePoint Update(TimePoint current_time) override;

  void Stop();

  std::vector<ServerDescriptor> const& server_descriptors();

 private:
  void RequestCloud(TimePoint current_time);
  void RequestServerResolve(TimePoint current_time);

  void OnCloudResponse(UidAndCloud const& uid_and_cloud);
  void OnServerResponse(ServerDescriptor const& server_descriptor);

  ClientToServerStream* client_to_server_stream_;
  Uid client_uid_;

  std::optional<AddHeaderGate> add_resolvers_;
  std::optional<GatesStream<SerializeGate<Uid, UidAndCloud>, StreamApiGate,
                            AddHeaderGate&>>
      cloud_request_stream_;
  std::optional<GatesStream<SerializeGate<ServerId, ServerDescriptor>,
                            StreamApiGate, AddHeaderGate&>>
      server_resolver_stream_;

  StateMachine<State> state_;

  Subscription state_changed_subscription_;
  Subscription cloud_response_subscription_;
  Subscription server_resolve_subscription_;

  ActionView<StreamWriteAction> cloud_request_action_;
  std::vector<ActionView<StreamWriteAction>> server_resolve_actions_;

  MultiSubscription cloud_request_subscriptions_;
  MultiSubscription server_resolve_subscriptions_;

  UidAndCloud uid_and_cloud_;
  std::vector<ServerDescriptor> server_descriptors_;
  TimePoint start_resolve_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_H_
