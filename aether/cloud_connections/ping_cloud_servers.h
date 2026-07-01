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

#ifndef AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
#define AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_

#include "aether/config.h"
#if AE_ENABLE_PING

#  include <map>
#  include <memory>
#  include <utility>

#  include "aether/ae_context.h"
#  include "aether/types/server_id.h"
#  include "aether/events/event_subscription.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/cloud_connections/cloud_server_connections.h"

#  include "aether/ae_actions/ping.h"

namespace ae {
class PingCloudServers {
  struct ServerPing {
    std::unique_ptr<Ping> ping;
    std::size_t priority;
    Subscription server_state_sub;
  };

 public:
  PingCloudServers(AeContext const& ae_context,
                   CloudServerConnections& cloud_server_connections);

 private:
  void ServersUpdate();
  void DispatchToServers();
  void MakePingToServer(CloudServerConnection& cloud_sc);
  std::tuple<ServerId, ServerPing&, bool> GetOrCreatePing(
      CloudServerConnection& cloud_sc);

  AeContext ae_context_;
  CloudServerConnections* cloud_server_connections_;

  Subscription servers_update_;
  TaskSubscription task_sub_;

  std::map<ServerId, ServerPing> server_pings_;
};
}  // namespace ae

#endif
#endif  // AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
