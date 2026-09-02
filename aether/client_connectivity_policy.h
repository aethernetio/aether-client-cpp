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
#include <optional>

#include "aether/config.h"
#include "aether/events/events.h"
#include "aether/obj/obj.h"

#include "aether/cloud_connections/request_policy.h"

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

enum class LocalConnectivityState {
  kWaitingFirstResponse,
  kOnline,
  kSuspect,
  kOffline,
};

enum class LocalConnectivityReason {
  kNoAuthenticatedResponse,
  kRecentCloudResponse,
  kSuspectAge,
  kOfflineAge,
  kPlannedPingGrace,
  kInFlightPingGrace,
};

enum class PingPhase {
  kNone,
  kPlanned,
  kInFlight,
};

struct PingCycleState {
  std::uint64_t cycle_id{0};
  PingPhase phase{PingPhase::kNone};
  LocalConnectivityState holds_state{LocalConnectivityState::kWaitingFirstResponse};
  TimePoint planned_at{};
  TimePoint dispatch_deadline{};
  TimePoint response_deadline{};
};

struct LocalConnectivitySnapshot {
  TimePoint now{};
  LocalConnectivityState state{LocalConnectivityState::kWaitingFirstResponse};
  LocalConnectivityReason reason{LocalConnectivityReason::kNoAuthenticatedResponse};

  bool has_any_cloud_response{false};
  TimePoint last_any_cloud_response{};
  Duration age_since_last_any_cloud_response{};

  bool has_ping_response{false};
  TimePoint last_ping_response{};
  Duration age_since_last_ping_response{};

  Duration ping_interval{};
  Duration offline_margin{};
  TimePoint online_until{};
  TimePoint suspect_until{};

  std::uint32_t planned_ping_count{0};
  std::uint32_t pings_in_flight{0};
  TimePoint nearest_dispatch_deadline{};
  TimePoint nearest_response_deadline{};
  bool active_grace{false};
  bool online{false};

  // Compatibility aliases for older callers.
  bool has_success{false};
  Duration age_since_last_success{};
  TimePoint last_success{};
  TimePoint recent_success_until{};
  TimePoint pending_ping_deadline{};
  bool in_flight_grace_active{false};
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
      policy_->rx_timings_[Priority].conf = conf;
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

  ConnectivityStatus GetStatus() const noexcept;
  void ResetRxTimings();
  void ResetRuntimeState();

  void ReportAuthenticatedCloudResponse(std::size_t priority, TimePoint at);
  void ReportPingPlanned(std::size_t priority, std::uint64_t cycle_id,
                         TimePoint planned_at, TimePoint dispatch_deadline,
                         LocalConnectivityState holds_state);
  void ReportPingDispatched(std::size_t priority, std::uint64_t cycle_id,
                            TimePoint send_time, Duration response_timeout);
  void ReportSuccessfulPingResponse(std::size_t priority,
                                    std::uint64_t cycle_id, TimePoint at);
  void ReportPingCompletedWithoutSuccess(std::size_t priority,
                                         std::uint64_t cycle_id, TimePoint at);
  void ReportPingCancelled(std::size_t priority, std::uint64_t cycle_id);
  std::uint64_t AllocatePingCycleId(std::size_t priority) noexcept;

  TimePoint last_successful_cloud_response() const noexcept;
  std::optional<Duration> TimeSinceLastSuccessfulCloudResponse(
      TimePoint now = Now()) const noexcept;
  std::optional<Duration> TimeSinceLastSuccessfulPingResponse(
      TimePoint now = Now()) const noexcept;
  bool IsLocallyOnline(TimePoint now = Now()) const noexcept;
  LocalConnectivitySnapshot InspectLocalConnectivity(
      TimePoint now = Now()) const noexcept;

  SuspendBlocker AcquireSuspendBlock();
  void ReportNextServiceTime(std::size_t priority, TimePoint next_service_time);

 private:
  struct PriorityConnectivityState {
    bool has_any_cloud_response{false};
    TimePoint last_any_cloud_response{};
    bool has_ping_response{false};
    TimePoint last_ping_response{};
    Duration offline_margin{};
    std::uint64_t next_cycle_id{1};
    PingCycleState ping_cycle{};
    PingCycleState scheduled_ping{};
  };

  static int StateRank(LocalConnectivityState state) noexcept;
  static LocalConnectivityState BaseStateForPriority(
      PriorityConnectivityState const& state, Duration interval,
      Duration offline_margin, TimePoint now) noexcept;
  static LocalConnectivityState ApplyPingGrace(
      LocalConnectivityState base, PingCycleState const& active,
      PingCycleState const& scheduled, TimePoint now) noexcept;
  static LocalConnectivityReason ReasonForState(
      LocalConnectivityState state, bool grace_active) noexcept;

  LocalConnectivityState PriorityState(std::size_t priority,
                                       TimePoint now) const noexcept;
  void ClearPriorityRuntimeState(std::size_t priority) noexcept;
  void ClearAllLocalConnectivityState() noexcept;
  void AdvanceAnyCloudResponse(std::size_t priority, TimePoint at) noexcept;
  void ClearPingCycleIfMatch(std::size_t priority,
                             std::uint64_t cycle_id) noexcept;
  Duration PingIntervalFor(std::size_t priority) const noexcept;
  void IncrementSuspendBlock();
  void DecrementSuspendBlock();

  RequestPolicy::Variant rx_targets_;
  std::array<RxTiming, kMaxRxServerPriorities> rx_timings_;
  std::array<PriorityConnectivityState, kMaxRxServerPriorities> priorities_{};

  bool can_suspend_{true};
  std::uint8_t suspend_block_count_{};

  Event<void()> suspend_allowed_event_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_CONNECTIVITY_POLICY_H_
