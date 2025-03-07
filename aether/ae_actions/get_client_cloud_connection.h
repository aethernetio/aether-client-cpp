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
#include <optional>

#include "aether/uid.h"
#include "aether/memory.h"
#include "aether/async_for_loop.h"
#include "aether/ptr/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

#include "aether/client_connections/client_connection.h"
#include "aether/client_connections/server_connection_selector.h"

#include "aether/ae_actions/get_client_cloud.h"

namespace ae {
class Client;
class Cloud;
class ClientConnectionManager;

class GetClientCloudConnection : public Action<GetClientCloudConnection> {
  enum class State : std::uint8_t {
    kTryCache,
    kSelectConnection,
    kConnection,
    kGetCloud,
    kCreateConnection,
    kSuccess,
    kStopped,
    kFailed
  };

 public:
  GetClientCloudConnection(
      ActionContext action_context,
      Ptr<ClientConnectionManager> const& client_connection_manager,
      ObjPtr<Client> const& client, Uid client_uid, ObjPtr<Cloud> const& cloud,
      std::unique_ptr<IServerConnectionFactory>&& server_connection_factory);

  ~GetClientCloudConnection() override;

  TimePoint Update(TimePoint current_time) override;

  void Stop();

  Ptr<ClientConnection> client_cloud_connection();

 private:
  void TryCache(TimePoint current_time);
  void SelectConnection(TimePoint current_time);
  void GetCloud(TimePoint current_time);
  void CreateConnection(TimePoint current_time);

  ActionContext action_context_;
  PtrView<Client> client_;
  Uid client_uid_;
  PtrView<ClientConnectionManager> client_connection_manager_;
  ServerConnectionSelector server_connection_selector_;
  std::optional<AsyncForLoop<RcPtr<ClientServerConnection>>>
      connection_selection_loop_;

  StateMachine<State> state_;

  RcPtr<ClientServerConnection> server_connection_;
  Subscription connection_subscription_;

  std::optional<GetClientCloudAction> get_client_cloud_action_;
  MultiSubscription get_client_cloud_subscriptions_;

  Ptr<ClientConnection> client_cloud_connection_;
  Subscription state_changed_subscription_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_CONNECTION_H_
