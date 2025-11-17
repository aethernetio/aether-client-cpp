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
#ifndef AETHER_CLIENT_CONNECTIONS_CLOUD_CONNECTION_H_
#define AETHER_CLIENT_CONNECTIONS_CLOUD_CONNECTION_H_

#include <vector>

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/timer_action.h"
#include "aether/types/small_function.h"
#include "aether/actions/notify_action.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"
#include "aether/connection_manager/client_connection_manager.h"

#include "aether/api_protocol/api_protocol.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae {
struct RequestPolicy {
  // Make request only to the main server (the highest priority).
  struct MainServer {};

  // Make request only to the server with provided priority.
  struct Priority {
    std::size_t priority;  //< The less, the higher priority
  };

  // Make count of request replicas to the servers.
  struct Replica {
    std::size_t count;
  };

  using Variant = std::variant<MainServer, Priority, Replica>;
};

class CloudConnection {
  using ReselectServers = NotifyAction;

 public:
  using ServerVisitor =
      SmallFunction<void(ServerConnection* server_connection)>;
  using AuthorizedApiCaller =
      SmallFunction<void(ApiContext<AuthorizedApi>& auth_api,
                         ServerConnection* server_connection)>;
  using ClientApiSubscriber = SmallFunction<MultiSubscription::EventHandler(
      ClientApiSafe& client_api, ServerConnection* server_connection)>;

  using ServersUpdate = Event<void()>;

  class ReplicaSubscription {
   public:
    ReplicaSubscription() = default;
    ReplicaSubscription(CloudConnection& cloud_connection,
                        CloudConnection::ClientApiSubscriber subscriber,
                        RequestPolicy::Variant request_policy);

    ReplicaSubscription(ReplicaSubscription&& other) noexcept;
    ReplicaSubscription& operator=(ReplicaSubscription&& other) noexcept;

    AE_CLASS_NO_COPY(ReplicaSubscription)

    void Reset();

   private:
    void ServersUpdate();

    CloudConnection* cloud_connection_;
    CloudConnection::ClientApiSubscriber subscriber_;
    RequestPolicy::Variant request_policy_;
    Subscription server_update_sub_;
    MultiSubscription subscriptions_;
  };

  CloudConnection(ActionContext action_context,
                  ClientConnectionManager& connection_manager,
                  std::size_t max_connections);

  /**
   * \brief Visit server streams with policy.
   */
  void VisitServers(
      ServerVisitor const& visitor,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});

  /**
   * \brief Access to authorized api with policy.
   */
  ActionPtr<StreamWriteAction> AuthorizedApiCall(
      AuthorizedApiCaller const& auth_api_caller,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});

  /**
   * \brief Access to client safe api with policy.
   */
  ReplicaSubscription ClientApiSubscription(
      ClientApiSubscriber subscriber,
      RequestPolicy::Variant policy = RequestPolicy::MainServer{});

  /**
   * \brief The event then top list of the servers were updated.
   */
  ServersUpdate::Subscriber servers_update_event();

  std::size_t count_connections() const;
  std::size_t max_connections() const;

  /**
   * \brief Restream all selected servers
   */
  void Restream();

 private:
  void VisitServers(RequestPolicy::MainServer, ServerVisitor const& visitor);
  void VisitServers(RequestPolicy::Priority priority,
                    ServerVisitor const& visitor);
  void VisitServers(RequestPolicy::Replica replica,
                    ServerVisitor const& visitor);

  void InitServers();
  void SelectServers();
  void SubscribeToServerState(ServerConnection* server_connection);

  void UnselectServer(ServerConnection* server_connection);
  void QuarantineTimer(ServerConnection* server_connection);

  ActionContext action_context_;
  ClientConnectionManager* connection_manager_;
  std::size_t max_connections_;

  OwnActionPtr<TimerAction> quarantine_timer_;
  OwnActionPtr<ReselectServers> reselect_servers_action_;
  // selected list of servers sorted by the priority
  std::vector<ServerConnection*> selected_servers_;

  ServersUpdate servers_update_event_;

  MultiSubscription server_state_subs_;
};
}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIONS_CLOUD_CONNECTION_H_
