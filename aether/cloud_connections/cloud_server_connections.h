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
#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_

#include <vector>

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/notify_action.h"
#include "aether/actions/action_context.h"
#include "aether/connection_manager/client_connection_manager.h"

namespace ae {

class CloudServerConnections {
  using ReselectServers = NotifyAction;
  friend class CloudRequest;

 public:
  using ServersUpdate = Event<void()>;

  CloudServerConnections(ActionContext action_context,
                         ClientConnectionManager& connection_manager,
                         std::size_t max_connections);

  /**
   * \brief The event then top list of the servers were updated.
   */
  ServersUpdate::Subscriber servers_update_event();
  /**
   * \brief List of currently selected servers in priority order
   */
  std::vector<CloudServerConnection*> const& servers() const;

  std::size_t count_connections() const;
  std::size_t max_connections() const;

  /**
   * \brief Restream all selected servers
   */
  void Restream();

 private:
  void InitServers();
  void SelectServers(std::vector<CloudServerConnection*> const& servers);
  void SubscribeToServerState(CloudServerConnection& server_connection);

  void UnselectServer(CloudServerConnection& server_connection);
  void QuarantineTimer(CloudServerConnection& server_connection);

  std::vector<CloudServerConnection*> ServerCandidates();

  ActionContext action_context_;
  ClientConnectionManager* connection_manager_;
  std::size_t max_connections_;

  OwnActionPtr<TimerAction> quarantine_timer_;
  OwnActionPtr<ReselectServers> reselect_servers_action_;
  // selected list of servers sorted by the priority
  std::vector<CloudServerConnection*> selected_servers_;

  ServersUpdate servers_update_event_;

  std::map<std::uintptr_t, Subscription> server_state_subs_;
};
}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_SERVER_CONNECTIONS_H_
