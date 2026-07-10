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
#  include <optional>

#  include "aether/ae_context.h"
#  include "aether/events/event_subscription.h"
#  include "aether/executors/executors.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/types/server_id.h"

#  include "aether/ae_actions/ping.h"
#  include "aether/client_connectivity_policy.h"
#  include "aether/cloud_connections/cloud_server_connections.h"
#  include "aether/server.h"

namespace ae {
class PingCloudServers {
  class ServerPing {
   public:
    ServerPing(AeContext const& ae_context, ClientConnectivityPolicy& policy,
               CloudServerConnection& cloud_sc, std::size_t priority);
    ~ServerPing();

    AE_CLASS_NO_COPY_MOVE(ServerPing)

    void Stop();

    TimePoint next_service_time() const noexcept { return next_ping_time_; }
    std::size_t priority() const noexcept { return priority_; }
    RxTimingConf const& timing() const noexcept { return timing_conf_; }

   private:
    void Start();

    template <typename F>
    void WaitForLink(ClientServerConnection& cc, F&& f);

    auto EnsureLinked();
    auto MakePing();

    void OnPingResult(Result<Duration, int> const& res);
    void OpenRxWindow(TimePoint sent_time);
    void ScheduleRestream();

    AeContext ae_context_;
    ClientConnectivityPolicy* policy_;
    CloudServerConnection* cloud_sc_;
    RxTimingConf timing_conf_{};
    std::size_t priority_{};

    std::optional<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>>
        waiter_;
    std::optional<Ping> ping_;
    bool stop_{false};
    Subscription link_state_sub_;
    TaskSubscription start_sub_;
    TaskSubscription rx_window_sub_;
    TaskSubscription restream_sub_;
    ClientConnectivityPolicy::SuspendBlocker ping_blocker_;
    ClientConnectivityPolicy::SuspendBlocker rx_window_blocker_;
    ClientConnectivityPolicy::SuspendBlocker restream_blocker_;
    TimePoint next_ping_time_;
  };

 public:
  PingCloudServers(AeContext const& ae_context,
                   CloudServerConnections& cloud_server_connections,
                   ClientConnectivityPolicy& policy);
  ~PingCloudServers();

 private:
  void ServersUpdate();
  void DispatchToServers();
  void ReconcileServer(Ptr<Server> const& server,
                       CloudServerConnection& cloud_sc);

  AeContext ae_context_;
  CloudServerConnections* cloud_server_connections_;
  ClientConnectivityPolicy* policy_;

  Subscription servers_update_;
  TaskSubscription task_sub_;

  std::map<ServerId, std::unique_ptr<ServerPing>> server_pings_;
};
}  // namespace ae

#endif
#endif  // AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
