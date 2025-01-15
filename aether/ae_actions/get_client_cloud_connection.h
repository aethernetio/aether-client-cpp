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
#include "aether/obj/ptr_view.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"

#include "aether/client_connections/client_connection.h"
#include "aether/client_connections/client_server_connection_selector.h"

#include "aether/ae_actions/get_client_cloud.h"

namespace ae {
class ClientConnectionManager;

class GetClientCloudConnection : public Action<GetClientCloudConnection> {
  enum class State : std::uint8_t {
    kTryCache,
    kSelectServer,
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
      Uid client_uid,
      Ptr<ClientServerConnectionSelector> client_server_connection_selector);

  ~GetClientCloudConnection() override;

  TimePoint Update(TimePoint current_time) override;

  void Stop();

  Ptr<ClientConnection> client_cloud_connection() const;

 private:
  void TryCache(TimePoint current_time);
  void SelectServer(TimePoint current_time);
  void GetCloud(TimePoint current_time);
  void CreateConnection(TimePoint current_time);

  Ptr<ClientConnection> CreateConnection(
      Ptr<ClientServerConnectionSelector> client_to_server_stream_selector);

  ActionContext action_context_;
  Uid client_uid_;
  PtrView<ClientConnectionManager> client_connection_manager_;
  Ptr<ClientServerConnectionSelector> client_server_connection_selector_;

  StateMachine<State> state_;

  Ptr<ClientServerConnection> client_server_connection_;
  Subscription connection_subscription_;
  Subscription disconnection_subscription_;

  std::optional<GetClientCloudAction> get_client_cloud_action_;
  MultiSubscription get_client_cloud_subscriptions_;

  Ptr<ClientConnection> client_cloud_connection_;
  Subscription state_changed_subscription_;
};
}  // namespace ae

#endif  // AETHER_AE_ACTIONS_GET_CLIENT_CLOUD_CONNECTION_H_