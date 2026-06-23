/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHET_CONNECTION_MANAGER_CONNECTION_MANAGHER_H_
#define AETHET_CONNECTION_MANAGER_CONNECTION_MANAGHER_H_

#include "aether/types/uid.h"
#include "aether/ae_context.h"
#include "aether/events/events.h"

#include "aether/cloud_connections/cloud_request.h"
#include "aether/connection_manager/get_cloud_action.h"
#include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class ClientCloudManager;
class CloudServerConnections;

class GetCloudFromAether final : public GetCloudAction {
 public:
  explicit GetCloudFromAether(AeContext const& ae_context,
                              ClientCloudManager& client_cloud_manager,
                              CloudServerConnections& cloud_connection,
                              Uid const& client_uid);

  ResultEvent::Subscriber result_event() noexcept override;

 private:
  void RequestCloud();
  void CloudUpdate(Uid const& uid, Result<Cloud::ptr const&, int> const& res);

  AeContext ae_context_;
  Uid client_uid_;

  Subscription cloud_update_sub_;
  CloudRequest cloud_request_;
  ResultEvent result_event_;
};
}  // namespace ae

#endif  // AETHET_CONNECTION_MANAGER_CONNECTION_MANAGHER_H_
