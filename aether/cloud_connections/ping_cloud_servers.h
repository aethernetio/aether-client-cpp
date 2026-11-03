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
#  include <utility>

#  include "aether/ae_context.h"
#  include "aether/executors/executors.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/types/server_id.h"

#  include "aether/ae_actions/ping.h"
#  include "aether/client_connectivity_policy.h"
#  include "aether/diagnostics/bench_ping_server_start_defer.h"
#  include "aether/cloud_connections/cloud_server_connections.h"

namespace ae {
class Channel;
struct PingCloudServersTestAccess;

class PingCloudServers {
  class ServerPing {
    friend struct PingCloudServersTestAccess;
   public:
    ServerPing(AeContext const& ae_context, ClientConnectivityPolicy& policy,
               CloudServerConnections& cloud_server_connections,
               CloudServerConnection& cloud_sc, std::size_t priority);
    ~ServerPing();

    AE_CLASS_NO_COPY_MOVE(ServerPing)

    void Stop();

    TimePoint next_service_time() const noexcept { return next_ping_time_; }
    std::size_t priority() const noexcept { return priority_; }
    RxTimingConf const& timing() const noexcept { return timing_conf_; }
    bool stopped() const noexcept { return stop_; }
    bool AwaitingLatePing() const noexcept;

   private:
    void Start();

    template <typename F>
    void WaitForLink(ClientServerConnection& cc, F&& f);

    auto EnsureLinked();
    auto MakePing();

    void OnPingResult(Ping::PingResult const& res);
    void OpenRxWindow(TimePoint sent_time);
    void ScheduleRestream();

    AeContext ae_context_;
    ClientConnectivityPolicy* policy_;
    CloudServerConnections* cloud_server_connections_;
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
    bench_ping_server_start::DeferState defer_state_;

    void ScheduleNextPeriodicStart();
    void HandlePingTimeoutOnScheduler(std::uint32_t epoch);
    void HandleLatePingOnScheduler(Ping::LateDuration const& late,
                                   std::uint32_t epoch);
    bool PingWaitBindingMatches(std::uint32_t epoch) const;

    template <typename F>
    void SchedulePingDeferred(std::uint32_t epoch, F&& fn);

    template <typename F>
    void SchedulePingFinish(std::uint32_t epoch, F&& fn);

    std::uint32_t ping_epoch_{0};
    ClientServerConnection* ping_wait_connection_{nullptr};
    Channel* ping_wait_channel_{nullptr};
    TaskSubscription ping_deferred_sub_;
    TaskSubscription ping_finish_sub_;
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

  AeContext ae_context_;
  CloudServerConnections* cloud_server_connections_;
  ClientConnectivityPolicy* policy_;

  Subscription servers_update_;
  Subscription server_quarantined_sub_;
  Subscription server_quarantine_released_sub_;
  TaskSubscription task_sub_;

  friend struct PingCloudServersTestAccess;

  std::map<ServerId, std::unique_ptr<ServerPing>> server_pings_;
};

/** Test-only hooks for ServerPing::Start deferral (see ping_cloud_servers.cpp). */
struct PingCloudServersTestAccess {
  static void TriggerPeriodicStart(PingCloudServers& pcs, ServerId server_id);
  static bool HasPendingClientPing(PingCloudServers& pcs, ServerId server_id);
  static bool HasAwaitingLateClientPing(PingCloudServers& pcs,
                                        ServerId server_id);
  static Ping const* ClientPingPtr(PingCloudServers& pcs, ServerId server_id);
  static void StopServerPing(PingCloudServers& pcs, ServerId server_id);
};

template <typename F>
void PingCloudServers::ServerPing::SchedulePingDeferred(std::uint32_t epoch,
                                                        F&& fn) {
  ping_deferred_sub_ = ae_context_.scheduler().Task(
      [this, epoch, f = std::forward<F>(fn)]() mutable {
        ping_deferred_sub_.Reset();
        if (stop_ || epoch != ping_epoch_ || !PingWaitBindingMatches(epoch)) {
          return;
        }
        f();
      });
}

template <typename F>
void PingCloudServers::ServerPing::SchedulePingFinish(std::uint32_t epoch,
                                                      F&& fn) {
  ping_finish_sub_ = ae_context_.scheduler().Task(
      [this, epoch, f = std::forward<F>(fn)]() mutable {
        ping_finish_sub_.Reset();
        if (stop_ || epoch != ping_epoch_ || !PingWaitBindingMatches(epoch)) {
          return;
        }
        f();
      });
}

}  // namespace ae

#endif
#endif  // AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
