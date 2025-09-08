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

#ifndef AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_CONNECTION_H_
#define AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_CONNECTION_H_

#include <cstdint>

#include "aether/types/uid.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

#include "aether/client_connections/client_connection.h"
#include "aether/connection_manager/client_cloud_manager.h"

namespace ae {
class Client;
class Cloud;
class ClientCloudManager;

class GetClientCloudConnection : public Action<GetClientCloudConnection> {
  enum class State : std::uint8_t {
    kGetCloud,
    kCreateConnection,
    kSuccess,
    kStopped,
    kFailed
  };

 public:
  GetClientCloudConnection(ActionContext action_context,
                           ObjPtr<Client> const& client, Uid client_uid);

  ~GetClientCloudConnection() override;

  UpdateStatus Update();

  void Stop();

  Ptr<ClientConnection> client_cloud_connection();

 private:
  void GetCloud();
  void CreateConnection();

  ActionContext action_context_;
  PtrView<Client> client_;
  Uid client_uid_;

  StateMachine<State> state_;
  ActionPtr<GetCloudAction> get_cloud_action_;
  PtrView<Cloud> new_cloud_;

  Ptr<ClientConnection> client_cloud_connection_;
  Subscription state_changed_subscription_;
  Subscription get_cloud_sub_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_CONNECTION_H_
