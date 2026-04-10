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

#include "aether/ae_context.h"
#include "aether/types/result.h"
#include "aether/events/events.h"
#include "aether/types/server_id.h"
#include "aether/actions/action2_.h"
#include "aether/events/multi_subscription.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class GetServersAction : public a2::Action {
 public:
  using ResultEvent =
      Event<void(Result<std::vector<ServerDescriptor> const&, int>)>;

  GetServersAction(AeContext const& ae_context,
                   std::vector<ServerId> server_ids,
                   CloudServerConnections& cloud_connection,
                   RequestPolicy::Variant request_policy);

  ResultEvent::Subscriber result_event();

 private:
  void GetResponse(ServerDescriptor const& server_descriptor,
                   CloudRequestAction* request);

  std::vector<ServerId> server_ids_;
  TimePoint timeout_point_;
  ResultEvent result_event_;

  CloudRequestAction cloud_request_;
  MultiSubscription request_subs_;

  std::vector<ServerDescriptor> server_descriptors_;
};
}  // namespace ae
#endif  // AETHER_AE_ACTIONS_GET_SERVERS_H_
