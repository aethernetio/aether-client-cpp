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

#ifndef AETHER_CLIENT_CONNECTIVITY_POLICY_H_
#define AETHER_CLIENT_CONNECTIVITY_POLICY_H_

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

#include "aether-objects/obj/obj.h"

#include "aether/clock.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/config.h"
#include "aether/events/events.h"
#include "aether/types/server_id.h"

#include <chrono>

namespace ae {

inline constexpr std::size_t kMaxRxServerPriorities{
    AE_CLOUD_MAX_SERVER_CONNECTIONS};
static_assert(kMaxRxServerPriorities > 0);

struct RxTimingConf {
  AE_REFLECT_MEMBERS(interval, rx_window)

  Duration interval{};
  Duration rx_window{};

  static constexpr RxTimingConf Every(Duration i) {
    return RxTimingConf{.interval = i, .rx_window = i};
  }

  constexpr RxTimingConf WithWindow(Duration rx_w) const {
    return RxTimingConf{.interval = interval, .rx_window = rx_w};
  }
};

struct RxTiming {
  AE_REFLECT_MEMBERS(conf, next_rx_point, recordet_at)

  RxTimingConf conf;
  TimePoint next_rx_point;
  TimePoint recordet_at;
};

struct ConnectivityStatus {
  bool can_suspend{true};
  std::uint8_t suspend_block_count{};
  TimePoint next_service_time;
};

struct ServerPresenceState {
  RxTimingConf desired{
      RxTimingConf::Every(std::chrono::milliseconds{AE_PING_INTERVAL_MS})};
  Percentile8 rtt_reliability_percentile{kDefaultRttReliabilityPercentile};

  bool has_confirmed_schedule{false};
  Duration confirmed_interval{};
  Duration confirmed_rx_window{};
  TimePoint confirmed_ping_send_time{};
  TimePoint confirmed_pong_receive_time{};
  TimePoint confirmed_window_open_local{};
  TimePoint confirmed_window_close_local{};

  bool config_change_pending{false};
  bool selected_for_aggregate{true};
  bool has_user_rx_timing{false};
  bool quarantined{false};
  std::size_t bound_priority{static_cast<std::size_t>(-1)};
};

class ClientConnectivityPolicy : public Obj {
  AE_OBJECT(ClientConnectivityPolicy, Obj, 0)

 public:
  class RxTimingConfig {
   public:
    RxTimingConfig(ClientConnectivityPolicy& policy,
                   RequestPolicy::Variant targets);

    RxTimingConfig& ForAllPriorities(RxTimingConf conf);
    template <std::size_t Priority>
    RxTimingConfig& ForPriority(RxTimingConf conf) {
      static_assert(Priority < kMaxRxServerPriorities);
      policy_->ApplyDesiredForPriority(Priority, conf);
      return *this;
    }

   private:
    ClientConnectivityPolicy* policy_;
  };

  class SuspendBlocker {
   public:
    SuspendBlocker() = default;
    explicit SuspendBlocker(ClientConnectivityPolicy& policy);
    ~SuspendBlocker();

    SuspendBlocker(SuspendBlocker&& other) noexcept;
    SuspendBlocker& operator=(SuspendBlocker&& other) noexcept;
    SuspendBlocker(SuspendBlocker const&) = delete;
    SuspendBlocker& operator=(SuspendBlocker const&) = delete;

    void Reset();

   private:
    ClientConnectivityPolicy* policy_{};
  };

  ClientConnectivityPolicy();
#ifdef AE_DISTILLATION
  explicit ClientConnectivityPolicy(ObjProp prop);
#endif

  AE_CLASS_NO_COPY_MOVE(ClientConnectivityPolicy);

  AE_OBJECT_REFLECT(AE_MMBRS(rx_targets_, rx_timings_))
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, rx_targets_, rx_timings_);
    ResetRuntimeState();
  }

  RxTimingConfig ConfigureRxTimings(
      RequestPolicy::Variant targets = RequestPolicy::All{});

  // Per-server runtime config. Does not invent ONLINE until a confirming Pong.
  void ConfigureServerRxTiming(
      ServerId server_id, RxTimingConf conf,
      Percentile8 rtt_reliability_percentile =
          kDefaultRttReliabilityPercentile);

  void SetServerSelectedForAggregate(ServerId server_id, bool selected);
  void BindServerPriority(ServerId server_id, std::size_t priority);
  void SetServerQuarantined(ServerId server_id, bool quarantined);
  void RemoveServerFromCloud(ServerId server_id);

  RequestPolicy::Variant const& rx_targets() const noexcept {
    return rx_targets_;
  }
  std::array<RxTiming, kMaxRxServerPriorities> const& rx_timings()
      const noexcept {
    return rx_timings_;
  }
  Event<void()>::Subscriber suspend_allowed_event() noexcept {
    return EventSubscriber{suspend_allowed_event_};
  }
  Event<void(ServerId)>::Subscriber server_rx_timing_changed_event() noexcept {
    return EventSubscriber{server_rx_timing_changed_event_};
  }

  ConnectivityStatus GetStatus() const noexcept;
  void ResetRxTimings();

  SuspendBlocker AcquireSuspendBlock();
  void ReportNextServiceTime(std::size_t priority, TimePoint next_service_time);

  ServerPresenceState& EnsureServerPresence(ServerId server_id);
  ServerPresenceState const* FindServerPresence(ServerId server_id) const noexcept;
  ServerPresenceState* FindServerPresence(ServerId server_id) noexcept;

  // Confirm schedule from a successful Pong using selected_rtt projection.
  void ConfirmServerPong(ServerId server_id, TimePoint send_time,
                         TimePoint pong_time, Duration interval,
                         Duration rx_window, Duration selected_rtt);

  void ClearServerPresence(ServerId server_id);

  // Application-level Local/Remote Presence classification timeout.
  // Not part of Ping / rx_window. Applies immediately (no new Ping required).
  void SetOfflineDetectionTimeout(Duration timeout) noexcept;
  Duration offline_detection_timeout() const noexcept {
    return offline_detection_timeout_;
  }

  // Runtime CloudRequest soft-timeout / retry / hedge policy (not wire).
  // Applies to NEW CloudRequest operations only (snapshot at construction).
  void SetCloudRequestExecutionPolicy(
      CloudRequestExecutionPolicy policy) noexcept;
  CloudRequestExecutionPolicy const& cloud_request_execution_policy()
      const noexcept {
    return cloud_request_execution_policy_;
  }

  // Read-only. No side effects. Aggregate OR: ONLINE iff any selected server
  // has confirmed interval>0 and now <= expected_open + offline_detection_timeout.
  bool IsLocallyOnline() const noexcept;
  bool IsLocallyOnline(TimePoint now) const noexcept;
  bool IsServerLocallyOnline(ServerId server_id, TimePoint now) const noexcept;

  // Read-only diagnostics for live harness (expected_open / deadline / last pong).
  struct LocalPresenceDiag {
    bool any_online{false};
    bool has_schedule{false};
    ServerId server_id{};
    TimePoint expected_open{};
    TimePoint offline_deadline{};
    TimePoint last_pong{};
  };
  LocalPresenceDiag DiagnoseLocalPresence(TimePoint now) const noexcept;

 private:
  void ResetRuntimeState();
  void IncrementSuspendBlock();
  void DecrementSuspendBlock();
  void ApplyDesiredForAllPriorities(RxTimingConf conf);
  void ApplyDesiredForPriority(std::size_t priority, RxTimingConf conf);
  void ApplyDesiredIfNoOverride(ServerId server_id, ServerPresenceState& state,
                                RxTimingConf conf);

  RequestPolicy::Variant rx_targets_;
  std::array<RxTiming, kMaxRxServerPriorities> rx_timings_;
  std::map<ServerId, ServerPresenceState> server_presence_;

  bool can_suspend_{true};
  std::uint8_t suspend_block_count_{};
  Duration offline_detection_timeout_{std::chrono::milliseconds{
      AE_OFFLINE_DETECTION_TIMEOUT_MS}};
  CloudRequestExecutionPolicy cloud_request_execution_policy_{};

  Event<void()> suspend_allowed_event_;
  Event<void(ServerId)> server_rx_timing_changed_event_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIVITY_POLICY_H_
