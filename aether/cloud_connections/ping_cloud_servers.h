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

#  include <cstdint>
#  include <map>
#  include <memory>
#  include <optional>

#  include "aether/ae_actions/ping.h"
#  include "aether/ae_context.h"
#  include "aether/client_connectivity_policy.h"
#  include "aether/cloud_connections/cloud_server_connections.h"
#  include "aether/cloud_connections/local_presence_schedule.h"
#  include "aether/events/event_subscription.h"
#  include "aether/executors/executors.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/types/server_id.h"

namespace ae {
class PingCloudServers {
  class ServerPing {
   public:
    ServerPing(AeContext const& ae_context, ClientConnectivityPolicy& policy,
               CloudServerConnection& cloud_sc, std::size_t priority);
    ~ServerPing();

    AE_CLASS_NO_COPY_MOVE(ServerPing)

    void Stop();
    void NotifyConfigChanged();

    TimePoint next_service_time() const noexcept { return next_ping_time_; }
    std::size_t priority() const noexcept { return priority_; }
    RxTimingConf const& timing() const noexcept { return active_conf_; }
    bool stopped() const noexcept { return stop_; }

   private:
    void ScheduleNext(TimePoint when, PingAttemptKind kind);
    void StartAttempt(PingAttemptKind kind);
    void Start();

    template <typename F>
    void WaitForLink(ClientServerConnection& cc, F&& f);

    auto EnsureLinked();
    Duration SelectedRtt() const;
    Duration AttemptTimeout(Duration rtt) const;

    void OnPingResult(std::uint64_t attempt_id, TimePoint send_time,
                      Duration sent_interval, Duration sent_window,
                      Ping::PingResult const& res);
    void ApplyConfirmedPong(TimePoint send_time, TimePoint pong_time,
                            Duration sent_interval, Duration sent_window);
    void OnAttemptTimeout(std::uint64_t attempt_id);
    void AfterFailedAttempt();
    void HoldRxUntil(TimePoint until);
    void ScheduleRestream();
    void AbandonInFlight();

    AeContext ae_context_;
    ClientConnectivityPolicy* policy_;
    CloudServerConnection* cloud_sc_;
    ServerId server_id_{};
    std::size_t priority_{};
    RxTimingConf active_conf_{};

    std::optional<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>>
        waiter_;
    std::optional<Ping> ping_;
    bool stop_{false};
    bool attempt_in_flight_{false};
    std::uint64_t active_attempt_id_{0};
    PingAttemptKind active_attempt_kind_{PingAttemptKind::kInitial};
    TimePoint active_send_time_{};
    Duration active_sent_interval_{};
    Duration active_sent_window_{};

    Subscription link_state_sub_;
    TaskSubscription start_sub_;
    TaskSubscription attempt_timeout_sub_;
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
  void ReconcileServer(CloudServerConnection& cloud_sc);
  void ServerQuarantined(CloudServerConnection* cloud_sc);
  void ServerQuarantineReleased(CloudServerConnection* cloud_sc);
  void OnServerRxTimingChanged(ServerId server_id);

  AeContext ae_context_;
  CloudServerConnections* cloud_server_connections_;
  ClientConnectivityPolicy* policy_;

  Subscription servers_update_;
  Subscription server_quarantined_sub_;
  Subscription server_quarantine_released_sub_;
  Subscription server_rx_timing_changed_sub_;
  TaskSubscription task_sub_;

  std::map<ServerId, std::unique_ptr<ServerPing>> server_pings_;
};
}  // namespace ae

#endif
#endif  // AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
