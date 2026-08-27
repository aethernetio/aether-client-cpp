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
#  include <variant>

#  include "aether/ae_context.h"
#  include "aether/events/event_subscription.h"
#  include "aether/executors/executors.h"
#  include "aether/tasks/manual_task_scheduler.h"
#  include "aether/types/server_id.h"

#  include "aether/ae_actions/ping.h"
#  include "aether/client_connectivity_policy.h"
#  include "aether/cloud_connections/cloud_server_connections.h"
#  include "aether/cloud_connections/ping_schedule_guard.h"

#  include "aether-miscpp/types/result.h"

namespace ae {

enum class PingTraceKind : std::uint8_t {
  kPrepared = 0,
  kSent = 1,
  kResult = 2,
  kRxCloseScheduled = 3,
  kRxClosed = 4,
  kCycleStarted = 5,
  kAttemptPrepared = 6,
  kRequestDropped = 7,
  kRequestSent = 8,
  kResponseIgnored = 9,
  kAttemptTimeout = 10,
  kRetryScheduled = 11,
  kCycleConfirmed = 12,
  kNextCycleScheduled = 13,
};

struct PingTraceEvent {
  PingTraceKind kind{PingTraceKind::kPrepared};
  ServerId server_id{};
  TimePoint planned_send_at{};
  TimePoint actual_send_at{};
  Duration early_by{};
  Duration base_rx_window{};
  Duration effective_wire_rx_window{};
  TimePoint required_rx_until{};
  TimePoint next_planned_send{};
  Duration min_rtt{};
  Duration p99_rtt{};
  Duration ping_guard{};
  std::uint64_t channel_generation{0};
  int result_type{-1};
  TimePoint event_time{};
  std::uint64_t logical_cycle_id{0};
  std::uint32_t physical_attempt_index{0};
  std::int32_t fault_mode{0};
  bool request_was_sent{false};
  bool response_was_ignored{false};
  TimePoint cycle_anchor{};
  TimePoint contract_deadline{};
  std::int64_t wire_next_connect_ms{0};
  Duration retry_delay{};
  TimePoint next_local_send_at{};
  Duration attempt_lead{};
  Duration retry_reserve{};
  Duration loss_timeout{};
  bool predeadline_retry_guaranteed{true};
};

using PingTraceHook = void (*)(PingTraceEvent const&);
void SetPingTraceHook(PingTraceHook hook) noexcept;

class PingCloudServers {
  class ServerPing {
   public:
    ServerPing(AeContext const& ae_context, PingCloudServers& owner,
               ClientConnectivityPolicy& policy,
               CloudServerConnection& cloud_sc, std::size_t priority);
    ~ServerPing();

    AE_CLASS_NO_COPY_MOVE(ServerPing)

    void Stop();
    void AnnounceUnknown();
    bool quarantined() const noexcept;

    TimePoint next_service_time() const noexcept { return next_ping_time_; }
    std::size_t priority() const noexcept { return priority_; }
    RxTimingConf const& timing() const noexcept { return timing_conf_; }
    bool stopped() const noexcept { return stop_; }

   private:
    void Start();

    template <typename F>
    void WaitForLink(ClientServerConnection& cc, F&& f);

    auto EnsureLinked();
    auto MakePing();

    void OnPingResult(Ping::PingResult const& res);
    void OpenRxWindow();
    void ScheduleRxWindowClose(TimePoint close_time);
    void CloseRxWindowNow();
    void MaybeCloseAfterWriteFailure();
    void EmitTrace(PingTraceKind kind, int result_type = -1) const;
    void ScheduleRestream();
    LogicalPingCycleState& Cycle();
    void ConfirmCycleAndScheduleNext();
    void ScheduleSameCycleRetry(bool restream_first);
    void ScheduleSameCycleRetryWithPreDeadlinePolicy(bool restream_first);
    bool ChannelLinkedAndWritable() const;

    AeContext ae_context_;
    PingCloudServers* owner_{};
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
    TimePoint next_ping_time_{};
    std::optional<TimePoint> planned_send_at_{};
    std::optional<TimePoint> required_rx_until_{};
    LocalRxWindowState local_rx_{};
    bool rx_window_held_{false};
    bool announce_unknown_{false};

    struct PingAttempt {
      ServerId server_id{};
      std::optional<TimePoint> planned_send_at{};
      TimePoint actual_send_at{};
      Duration early_by{};
      Duration base_rx_window{};
      Duration effective_wire_rx_window{};
      TimePoint required_rx_until{};
      std::optional<TimePoint> required_rx_until_before{};
      TimePoint next_planned_send{};
      Duration min_rtt{};
      Duration p99_rtt{};
      Duration ping_guard{};
      std::uint64_t channel_generation{0};
      bool write_failed{false};
      std::uint64_t logical_cycle_id{0};
      std::uint32_t physical_attempt_index{0};
      std::int32_t fault_mode{0};
      bool request_was_sent{true};
      bool response_was_ignored{false};
      TimePoint cycle_anchor{};
      TimePoint contract_deadline{};
      std::int64_t wire_next_connect_ms{0};
      Duration retry_delay{};
      TimePoint next_local_send_at{};
      Duration attempt_lead{};
      Duration retry_reserve{};
      Duration loss_timeout{};
      bool predeadline_retry_guaranteed{true};
    };
    std::optional<PingAttempt> in_flight_{};
    std::uint64_t send_generation_{0};
  };

 public:
  PingCloudServers(AeContext const& ae_context,
                   CloudServerConnections& cloud_server_connections,
                   ClientConnectivityPolicy& policy);
  ~PingCloudServers();

  void StopAutomaticPing() noexcept;
  void BeginAnnounceUnknown();
  using AnnounceEvent = Event<void(Result<std::monostate, int>)>;
  AnnounceEvent::Subscriber announce_event();

 private:
  void ServersUpdate();
  void DispatchToServers();
  void ReconcileServer(CloudServerConnection& cloud_sc);
  void ServerQuarantined(CloudServerConnection* cloud_sc);
  void ServerQuarantineReleased(CloudServerConnection* cloud_sc);
  void OnAnnounceServerDone(bool ok);

  AeContext ae_context_;
  CloudServerConnections* cloud_server_connections_;
  ClientConnectivityPolicy* policy_;

  Subscription servers_update_;
  Subscription server_quarantined_sub_;
  Subscription server_quarantine_released_sub_;
  TaskSubscription task_sub_;

  std::map<ServerId, LogicalPingCycleState> cycle_states_;
  std::map<ServerId, std::unique_ptr<ServerPing>> server_pings_;
  bool auto_ping_enabled_{true};
  std::size_t announce_pending_{0};
  bool announce_in_progress_{false};
  bool announce_any_ok_{false};
  AnnounceEvent announce_event_;
};
}  // namespace ae

#endif
#endif  // AETHER_CLOUD_CONNECTIONS_PING_CLOUD_SERVERS_H_
