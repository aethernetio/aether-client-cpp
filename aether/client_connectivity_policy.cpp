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

#include "aether/client_connectivity_policy.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace ae {

namespace {

constexpr RxTiming kDefaultRxTiming{
    .conf = RxTimingConf::Every(
        std::chrono::duration_cast<Duration>(
            std::chrono::milliseconds{AE_PING_INTERVAL_MS})),
    .next_rx_point = {},
    .recordet_at = {},
};

std::array<RxTiming, kMaxRxServerPriorities> MakeDefaultRxTimings() {
  std::array<RxTiming, kMaxRxServerPriorities> timings{};
  timings.fill(kDefaultRxTiming);
  return timings;
}

Duration AgeSince(TimePoint now, TimePoint then) noexcept {
  if (now <= then) {
    return Duration{};
  }
  return std::chrono::duration_cast<Duration>(now - then);
}

void ClearRxSchedulePoints(std::array<RxTiming, kMaxRxServerPriorities>& timings) {
  for (auto& timing : timings) {
    timing.next_rx_point = TimePoint{};
    timing.recordet_at = TimePoint{};
  }
}

}  // namespace

ClientConnectivityPolicy::ClientConnectivityPolicy()
    : rx_timings_{MakeDefaultRxTimings()} {
  ResetRuntimeState();
}

#ifdef AE_DISTILLATION
ClientConnectivityPolicy::ClientConnectivityPolicy(ObjProp prop)
    : Obj{prop}, rx_timings_{MakeDefaultRxTimings()} {
  ResetRuntimeState();
}
#endif

ClientConnectivityPolicy::RxTimingConfig::RxTimingConfig(
    ClientConnectivityPolicy& policy, RequestPolicy::Variant targets)
    : policy_{&policy} {
  policy_->rx_targets_ = targets;
}

ClientConnectivityPolicy::RxTimingConfig&
ClientConnectivityPolicy::RxTimingConfig::ForAllPriorities(RxTimingConf conf) {
  for (auto& timing : policy_->rx_timings_) {
    timing.conf = conf;
  }
  return *this;
}

ClientConnectivityPolicy::SuspendBlocker::SuspendBlocker(
    ClientConnectivityPolicy& policy)
    : policy_{&policy} {
  policy_->IncrementSuspendBlock();
}

ClientConnectivityPolicy::SuspendBlocker::~SuspendBlocker() { Reset(); }

ClientConnectivityPolicy::SuspendBlocker::SuspendBlocker(
    SuspendBlocker&& other) noexcept
    : policy_{other.policy_} {
  other.policy_ = nullptr;
}

ClientConnectivityPolicy::SuspendBlocker& ClientConnectivityPolicy::SuspendBlocker::
operator=(SuspendBlocker&& other) noexcept {
  if (this != &other) {
    Reset();
    policy_ = other.policy_;
    other.policy_ = nullptr;
  }
  return *this;
}

void ClientConnectivityPolicy::SuspendBlocker::Reset() {
  if (policy_ != nullptr) {
    policy_->DecrementSuspendBlock();
    policy_ = nullptr;
  }
}

ClientConnectivityPolicy::RxTimingConfig ClientConnectivityPolicy::ConfigureRxTimings(
    RequestPolicy::Variant targets) {
  return RxTimingConfig{*this, targets};
}

ConnectivityStatus ClientConnectivityPolicy::GetStatus() const noexcept {
  return ConnectivityStatus{
      .can_suspend = can_suspend_,
      .suspend_block_count = suspend_block_count_,
      .next_service_time = TimePoint::max(),
  };
}

void ClientConnectivityPolicy::ResetRxTimings() {
  ClearRxSchedulePoints(rx_timings_);
  ClearAllLocalConnectivityState();
}

void ClientConnectivityPolicy::ClearPriorityRuntimeState(
    std::size_t priority) noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  priorities_[priority] = PriorityConnectivityState{};
}

void ClientConnectivityPolicy::ClearAllLocalConnectivityState() noexcept {
  for (auto& state : priorities_) {
    state = PriorityConnectivityState{};
  }
}

void ClientConnectivityPolicy::ResetRuntimeState() {
  ClearAllLocalConnectivityState();
}

int ClientConnectivityPolicy::StateRank(
    LocalConnectivityState state) noexcept {
  switch (state) {
    case LocalConnectivityState::kWaitingFirstResponse:
      return 0;
    case LocalConnectivityState::kOffline:
      return 1;
    case LocalConnectivityState::kSuspect:
      return 2;
    case LocalConnectivityState::kOnline:
      return 3;
  }
  return 0;
}

Duration ClientConnectivityPolicy::PingIntervalFor(
    std::size_t priority) const noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return Duration{};
  }
  return rx_timings_[priority].conf.interval;
}

LocalConnectivityState ClientConnectivityPolicy::BaseStateForPriority(
    PriorityConnectivityState const& state, Duration interval,
    Duration offline_margin, TimePoint now) noexcept {
  if (!state.has_any_cloud_response) {
    return LocalConnectivityState::kWaitingFirstResponse;
  }
  if (interval <= Duration{}) {
    return LocalConnectivityState::kOnline;
  }
  Duration const age = AgeSince(now, state.last_any_cloud_response);
  if (age < interval) {
    return LocalConnectivityState::kOnline;
  }
  Duration const offline_threshold = interval + interval + offline_margin;
  if (age < offline_threshold) {
    return LocalConnectivityState::kSuspect;
  }
  return LocalConnectivityState::kOffline;
}

LocalConnectivityState ClientConnectivityPolicy::ApplyPingGrace(
    LocalConnectivityState base, PingCycleState const& active,
    PingCycleState const& scheduled, TimePoint now) noexcept {
  auto apply_one = [&](LocalConnectivityState state,
                       PingCycleState const& cycle) {
    if (cycle.phase == PingPhase::kNone) {
      return state;
    }
    TimePoint const deadline =
        cycle.phase == PingPhase::kPlanned ? cycle.dispatch_deadline
                                           : cycle.response_deadline;
    if (deadline <= TimePoint{} || now >= deadline) {
      return state;
    }
    switch (cycle.holds_state) {
      case LocalConnectivityState::kOnline:
        if (state == LocalConnectivityState::kOnline ||
            state == LocalConnectivityState::kSuspect) {
          return LocalConnectivityState::kOnline;
        }
        break;
      case LocalConnectivityState::kSuspect:
        if (state == LocalConnectivityState::kSuspect) {
          return LocalConnectivityState::kSuspect;
        }
        break;
      default:
        break;
    }
    return state;
  };
  return apply_one(apply_one(base, active), scheduled);
}

LocalConnectivityReason ClientConnectivityPolicy::ReasonForState(
    LocalConnectivityState state, bool grace_active) noexcept {
  if (grace_active) {
    return LocalConnectivityReason::kPlannedPingGrace;
  }
  switch (state) {
    case LocalConnectivityState::kWaitingFirstResponse:
      return LocalConnectivityReason::kNoAuthenticatedResponse;
    case LocalConnectivityState::kOnline:
      return LocalConnectivityReason::kRecentCloudResponse;
    case LocalConnectivityState::kSuspect:
      return LocalConnectivityReason::kSuspectAge;
    case LocalConnectivityState::kOffline:
      return LocalConnectivityReason::kOfflineAge;
  }
  return LocalConnectivityReason::kNoAuthenticatedResponse;
}

void ClientConnectivityPolicy::AdvanceAnyCloudResponse(std::size_t priority,
                                                       TimePoint at) noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  auto& state = priorities_[priority];
  if (state.has_any_cloud_response && at < state.last_any_cloud_response) {
    return;
  }
  state.has_any_cloud_response = true;
  state.last_any_cloud_response = at;
}

void ClientConnectivityPolicy::ClearPingCycleIfMatch(
    std::size_t priority, std::uint64_t cycle_id) noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  auto& priority_state = priorities_[priority];
  if (priority_state.ping_cycle.phase != PingPhase::kNone &&
      priority_state.ping_cycle.cycle_id == cycle_id) {
    priority_state.ping_cycle = PingCycleState{};
  }
  if (priority_state.scheduled_ping.phase != PingPhase::kNone &&
      priority_state.scheduled_ping.cycle_id == cycle_id) {
    priority_state.scheduled_ping = PingCycleState{};
  }
}

void ClientConnectivityPolicy::ReportAuthenticatedCloudResponse(
    std::size_t priority, TimePoint at) {
  AdvanceAnyCloudResponse(priority, at);
}

void ClientConnectivityPolicy::ReportPingPlanned(
    std::size_t priority, std::uint64_t cycle_id, TimePoint planned_at,
    TimePoint dispatch_deadline, LocalConnectivityState holds_state) {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  auto& priority_state = priorities_[priority];
  PingCycleState* target = &priority_state.ping_cycle;
  if (priority_state.ping_cycle.phase == PingPhase::kInFlight) {
    target = &priority_state.scheduled_ping;
  }
  if (target->phase != PingPhase::kNone && target->cycle_id > cycle_id) {
    return;
  }
  target->cycle_id = cycle_id;
  target->phase = PingPhase::kPlanned;
  target->holds_state = holds_state;
  target->planned_at = planned_at;
  target->dispatch_deadline = dispatch_deadline;
  target->response_deadline = TimePoint{};
}

void ClientConnectivityPolicy::ReportPingDispatched(
    std::size_t priority, std::uint64_t cycle_id, TimePoint send_time,
    Duration response_timeout) {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  auto& priority_state = priorities_[priority];
  auto& cycle = priority_state.ping_cycle;
  if (cycle.phase == PingPhase::kNone &&
      priority_state.scheduled_ping.phase == PingPhase::kPlanned &&
      priority_state.scheduled_ping.cycle_id == cycle_id) {
    cycle = priority_state.scheduled_ping;
    priority_state.scheduled_ping = PingCycleState{};
  } else if (cycle.phase != PingPhase::kNone && cycle.cycle_id != cycle_id) {
    return;
  } else if (cycle.phase == PingPhase::kNone &&
             priority_state.scheduled_ping.cycle_id != cycle_id &&
             priority_state.scheduled_ping.phase == PingPhase::kPlanned) {
    return;
  }
  cycle.cycle_id = cycle_id;
  cycle.phase = PingPhase::kInFlight;
  if (cycle.holds_state == LocalConnectivityState::kWaitingFirstResponse) {
    cycle.holds_state = BaseStateForPriority(
        priority_state, PingIntervalFor(priority), priority_state.offline_margin,
        send_time);
  }
  cycle.planned_at = send_time;
  cycle.dispatch_deadline = send_time;
  cycle.response_deadline = send_time + response_timeout;
}

void ClientConnectivityPolicy::ReportSuccessfulPingResponse(
    std::size_t priority, std::uint64_t cycle_id, TimePoint at) {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  auto& priority_state = priorities_[priority];
  auto& cycle = priority_state.ping_cycle;
  if (cycle.phase != PingPhase::kNone && cycle.cycle_id != cycle_id) {
    return;
  }
  AdvanceAnyCloudResponse(priority, at);
  if (cycle.response_deadline > cycle.planned_at) {
    priority_state.offline_margin =
        std::chrono::duration_cast<Duration>(cycle.response_deadline -
                                             cycle.planned_at);
  }
  if (!priority_state.has_ping_response || at >= priority_state.last_ping_response) {
    priority_state.has_ping_response = true;
    priority_state.last_ping_response = at;
  }
  ClearPingCycleIfMatch(priority, cycle_id);
}

void ClientConnectivityPolicy::ReportPingCompletedWithoutSuccess(
    std::size_t priority, std::uint64_t cycle_id, TimePoint at) {
  (void)at;
  ClearPingCycleIfMatch(priority, cycle_id);
}

void ClientConnectivityPolicy::ReportPingCancelled(
    std::size_t priority, std::uint64_t cycle_id) {
  ClearPingCycleIfMatch(priority, cycle_id);
}

std::uint64_t ClientConnectivityPolicy::AllocatePingCycleId(
    std::size_t priority) noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return 0;
  }
  return priorities_[priority].next_cycle_id++;
}

LocalConnectivityState ClientConnectivityPolicy::PriorityState(
    std::size_t priority, TimePoint now) const noexcept {
  if (priority >= kMaxRxServerPriorities) {
    return LocalConnectivityState::kWaitingFirstResponse;
  }
  auto const& priority_state = priorities_[priority];
  Duration const interval = PingIntervalFor(priority);
  Duration const margin = priority_state.offline_margin;
  LocalConnectivityState const base = BaseStateForPriority(
      priority_state, interval, margin, now);
  return ApplyPingGrace(base, priority_state.ping_cycle,
                        priority_state.scheduled_ping, now);
}

TimePoint ClientConnectivityPolicy::last_successful_cloud_response()
    const noexcept {
  TimePoint latest{};
  bool found = false;
  for (auto const& state : priorities_) {
    if (!state.has_any_cloud_response) {
      continue;
    }
    if (!found || state.last_any_cloud_response > latest) {
      latest = state.last_any_cloud_response;
      found = true;
    }
  }
  return found ? latest : TimePoint{};
}

std::optional<Duration>
ClientConnectivityPolicy::TimeSinceLastSuccessfulCloudResponse(
    TimePoint now) const noexcept {
  TimePoint latest{};
  bool found = false;
  for (auto const& state : priorities_) {
    if (!state.has_any_cloud_response) {
      continue;
    }
    if (!found || state.last_any_cloud_response > latest) {
      latest = state.last_any_cloud_response;
      found = true;
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return AgeSince(now, latest);
}

std::optional<Duration>
ClientConnectivityPolicy::TimeSinceLastSuccessfulPingResponse(
    TimePoint now) const noexcept {
  TimePoint latest{};
  bool found = false;
  for (auto const& state : priorities_) {
    if (!state.has_ping_response) {
      continue;
    }
    if (!found || state.last_ping_response > latest) {
      latest = state.last_ping_response;
      found = true;
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return AgeSince(now, latest);
}

bool ClientConnectivityPolicy::IsLocallyOnline(TimePoint now) const noexcept {
  LocalConnectivityState const state =
      InspectLocalConnectivity(now).state;
  return state == LocalConnectivityState::kOnline ||
         state == LocalConnectivityState::kSuspect;
}

LocalConnectivitySnapshot ClientConnectivityPolicy::InspectLocalConnectivity(
    TimePoint now) const noexcept {
  LocalConnectivitySnapshot snapshot{};
  snapshot.now = now;

  bool any_response = false;
  bool any_online = false;
  bool any_suspect = false;
  bool any_offline_with_response = false;

  TimePoint nearest_dispatch = TimePoint::max();
  TimePoint nearest_response = TimePoint::max();
  Duration interval_sum{};
  std::size_t interval_count = 0;
  Duration margin_max{};
  std::uint32_t planned_count = 0;
  std::uint32_t in_flight_count = 0;
  bool grace_active = false;

  for (std::size_t priority = 0; priority < kMaxRxServerPriorities; ++priority) {
    auto const& priority_state = priorities_[priority];
    if (priority_state.has_any_cloud_response) {
      any_response = true;
      if (!snapshot.has_any_cloud_response ||
          priority_state.last_any_cloud_response >
              snapshot.last_any_cloud_response) {
        snapshot.has_any_cloud_response = true;
        snapshot.last_any_cloud_response =
            priority_state.last_any_cloud_response;
      }
    }
    if (priority_state.has_ping_response) {
      if (!snapshot.has_ping_response ||
          priority_state.last_ping_response > snapshot.last_ping_response) {
        snapshot.has_ping_response = true;
        snapshot.last_ping_response = priority_state.last_ping_response;
      }
    }

    Duration const interval = PingIntervalFor(priority);
    if (interval > Duration{}) {
      interval_sum += interval;
      ++interval_count;
    }
    if (priority_state.offline_margin > margin_max) {
      margin_max = priority_state.offline_margin;
    }

    auto const& cycle = priority_state.ping_cycle;
    auto const& scheduled = priority_state.scheduled_ping;
    if (cycle.phase == PingPhase::kPlanned) {
      ++planned_count;
      if (cycle.dispatch_deadline > TimePoint{} &&
          cycle.dispatch_deadline < nearest_dispatch) {
        nearest_dispatch = cycle.dispatch_deadline;
      }
      if (cycle.dispatch_deadline > now) {
        grace_active = true;
      }
    } else if (scheduled.phase == PingPhase::kPlanned) {
      ++planned_count;
      if (scheduled.dispatch_deadline > TimePoint{} &&
          scheduled.dispatch_deadline < nearest_dispatch) {
        nearest_dispatch = scheduled.dispatch_deadline;
      }
      if (scheduled.dispatch_deadline > now) {
        grace_active = true;
      }
    } else if (cycle.phase == PingPhase::kInFlight) {
      ++in_flight_count;
      if (cycle.response_deadline > TimePoint{} &&
          cycle.response_deadline < nearest_response) {
        nearest_response = cycle.response_deadline;
      }
      if (cycle.response_deadline > now) {
        grace_active = true;
      }
    }

    LocalConnectivityState const priority_state_value =
        PriorityState(priority, now);
    switch (priority_state_value) {
      case LocalConnectivityState::kOnline:
        any_online = true;
        break;
      case LocalConnectivityState::kSuspect:
        any_suspect = true;
        break;
      case LocalConnectivityState::kOffline:
        if (priority_state.has_any_cloud_response) {
          any_offline_with_response = true;
        }
        break;
      case LocalConnectivityState::kWaitingFirstResponse:
        break;
    }
  }

  if (!any_response) {
    snapshot.state = LocalConnectivityState::kWaitingFirstResponse;
  } else if (any_online) {
    snapshot.state = LocalConnectivityState::kOnline;
  } else if (any_suspect) {
    snapshot.state = LocalConnectivityState::kSuspect;
  } else if (any_offline_with_response) {
    snapshot.state = LocalConnectivityState::kOffline;
  } else {
    snapshot.state = LocalConnectivityState::kWaitingFirstResponse;
  }

  if (snapshot.has_any_cloud_response) {
    snapshot.age_since_last_any_cloud_response =
        AgeSince(now, snapshot.last_any_cloud_response);
  }
  if (snapshot.has_ping_response) {
    snapshot.age_since_last_ping_response =
        AgeSince(now, snapshot.last_ping_response);
  }

  snapshot.ping_interval =
      interval_count > 0 ? interval_sum / static_cast<int>(interval_count)
                         : Duration{};
  snapshot.offline_margin = margin_max;
  snapshot.planned_ping_count = planned_count;
  snapshot.pings_in_flight = in_flight_count;
  snapshot.nearest_dispatch_deadline =
      nearest_dispatch == TimePoint::max() ? TimePoint{} : nearest_dispatch;
  snapshot.nearest_response_deadline =
      nearest_response == TimePoint::max() ? TimePoint{} : nearest_response;
  snapshot.active_grace = grace_active;

  if (snapshot.ping_interval > Duration{} &&
      snapshot.has_any_cloud_response) {
    snapshot.online_until =
        snapshot.last_any_cloud_response + snapshot.ping_interval;
    snapshot.suspect_until =
        snapshot.last_any_cloud_response + snapshot.ping_interval +
        snapshot.ping_interval + snapshot.offline_margin;
  }

  snapshot.reason = ReasonForState(snapshot.state, grace_active);
  snapshot.online = snapshot.state == LocalConnectivityState::kOnline ||
                    snapshot.state == LocalConnectivityState::kSuspect;

  snapshot.has_success = snapshot.has_any_cloud_response;
  snapshot.age_since_last_success = snapshot.age_since_last_any_cloud_response;
  snapshot.last_success = snapshot.last_any_cloud_response;
  snapshot.recent_success_until = snapshot.online_until;
  snapshot.pending_ping_deadline = snapshot.nearest_response_deadline;
  snapshot.in_flight_grace_active =
      in_flight_count > 0 && snapshot.nearest_response_deadline > now;

  return snapshot;
}

ClientConnectivityPolicy::SuspendBlocker
ClientConnectivityPolicy::AcquireSuspendBlock() {
  return SuspendBlocker{*this};
}

void ClientConnectivityPolicy::ReportNextServiceTime(
    std::size_t priority, TimePoint next_service_time) {
  if (priority >= kMaxRxServerPriorities) {
    return;
  }
  rx_timings_[priority].next_rx_point = next_service_time;
  rx_timings_[priority].recordet_at = Now();
}

void ClientConnectivityPolicy::IncrementSuspendBlock() {
  ++suspend_block_count_;
  if (can_suspend_) {
    can_suspend_ = false;
    suspend_allowed_event_.Emit();
  }
}

void ClientConnectivityPolicy::DecrementSuspendBlock() {
  if (suspend_block_count_ > 0) {
    --suspend_block_count_;
  }
  if (suspend_block_count_ == 0 && !can_suspend_) {
    can_suspend_ = true;
    suspend_allowed_event_.Emit();
  }
}

}  // namespace ae
