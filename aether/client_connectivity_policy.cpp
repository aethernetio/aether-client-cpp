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

#include <chrono>
#include <utility>

namespace ae {

namespace {
constexpr auto kDefaultTiming = RxTiming{
    .conf = RxTimingConf::Every(std::chrono::milliseconds{AE_PING_INTERVAL_MS}),
    .next_rx_point = {},
    .recordet_at = {}};

std::array<RxTiming, kMaxRxServerPriorities> MakeDefaultRxTimings() {
  std::array<RxTiming, kMaxRxServerPriorities> timings{};
  timings.fill(kDefaultTiming);
  return timings;
}
}  // namespace

ClientConnectivityPolicy::RxTimingConfig::RxTimingConfig(
    ClientConnectivityPolicy& policy, RequestPolicy::Variant targets)
    : policy_{&policy} {
  policy_->rx_targets_ = std::move(targets);
}

ClientConnectivityPolicy::RxTimingConfig&
ClientConnectivityPolicy::RxTimingConfig::ForAllPriorities(RxTimingConf conf) {
  policy_->ApplyDesiredForAllPriorities(conf);
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
    : policy_{std::exchange(other.policy_, nullptr)} {}

ClientConnectivityPolicy::SuspendBlocker&
ClientConnectivityPolicy::SuspendBlocker::operator=(
    SuspendBlocker&& other) noexcept {
  if (this != &other) {
    Reset();
    policy_ = std::exchange(other.policy_, nullptr);
  }
  return *this;
}

void ClientConnectivityPolicy::SuspendBlocker::Reset() {
  if (policy_ != nullptr) {
    policy_->DecrementSuspendBlock();
    policy_ = nullptr;
  }
}

ClientConnectivityPolicy::ClientConnectivityPolicy()
    : rx_targets_{RequestPolicy::All{}}, rx_timings_{MakeDefaultRxTimings()} {}

#ifdef AE_DISTILLATION
ClientConnectivityPolicy::ClientConnectivityPolicy(ObjProp prop)
    : Base{prop},
      rx_targets_{RequestPolicy::All{}},
      rx_timings_{MakeDefaultRxTimings()} {}
#endif

auto ClientConnectivityPolicy::ConfigureRxTimings(
    RequestPolicy::Variant targets) -> RxTimingConfig {
  return RxTimingConfig{*this, std::move(targets)};
}

void ClientConnectivityPolicy::ConfigureServerRxTiming(
    ServerId server_id, RxTimingConf conf,
    std::uint8_t rtt_reliability_percentile) {
  auto& state = EnsureServerPresence(server_id);
  auto const timing_changed = (state.desired.interval != conf.interval) ||
                              (state.desired.rx_window != conf.rx_window);
  state.desired = conf;
  state.has_user_rx_timing = true;
  state.rtt_reliability_percentile =
      rtt_reliability_percentile == 0 ? kDefaultRttReliabilityPercentile
                                      : rtt_reliability_percentile;
  if (state.rtt_reliability_percentile > 100) {
    state.rtt_reliability_percentile = 100;
  }
  // Confirmed schedule stays old until a Pong for a Ping carrying the new conf.
  if (timing_changed) {
    state.config_change_pending = true;
  }
  server_rx_timing_changed_event_.Emit(server_id);
}

void ClientConnectivityPolicy::SetServerSelectedForAggregate(ServerId server_id,
                                                             bool selected) {
  EnsureServerPresence(server_id).selected_for_aggregate = selected;
}

void ClientConnectivityPolicy::BindServerPriority(ServerId server_id,
                                                  std::size_t priority) {
  auto& state = EnsureServerPresence(server_id);
  state.bound_priority = priority;
  if (!state.has_user_rx_timing && (priority < rx_timings_.size())) {
    ApplyDesiredIfNoOverride(server_id, state, rx_timings_[priority].conf);
  }
}

void ClientConnectivityPolicy::SetServerQuarantined(ServerId server_id,
                                                    bool quarantined) {
  EnsureServerPresence(server_id).quarantined = quarantined;
}

void ClientConnectivityPolicy::RemoveServerFromCloud(ServerId server_id) {
  ClearServerPresence(server_id);
}

ClientConnectivityPolicy::SuspendBlocker
ClientConnectivityPolicy::AcquireSuspendBlock() {
  return SuspendBlocker{*this};
}

ConnectivityStatus ClientConnectivityPolicy::GetStatus() const noexcept {
  auto current_time = Now();
  auto next_service_time = TimePoint::max();
  for (auto const& t : rx_timings_) {
    next_service_time = std::min(
        next_service_time,
        (t.recordet_at > current_time) ? current_time : t.next_rx_point);
  }
  for (auto const& [id, state] : server_presence_) {
    static_cast<void>(id);
    if (state.has_confirmed_schedule &&
        state.confirmed_window_open_local != TimePoint{}) {
      next_service_time =
          std::min(next_service_time, state.confirmed_window_open_local);
    }
  }
  return ConnectivityStatus{.can_suspend = can_suspend_,
                            .suspend_block_count = suspend_block_count_,
                            .next_service_time = next_service_time};
}

void ClientConnectivityPolicy::ResetRxTimings() {
  for (auto& t : rx_timings_) {
    t.next_rx_point = {};
    t.recordet_at = {};
  }
}

void ClientConnectivityPolicy::ReportNextServiceTime(
    std::size_t priority, TimePoint next_service_time) {
  assert(priority < rx_timings_.size() && "Invalid priority value");

  auto& t = rx_timings_.at(priority);
  t.next_rx_point = next_service_time;
  t.recordet_at = Now();
}

ServerPresenceState& ClientConnectivityPolicy::EnsureServerPresence(
    ServerId server_id) {
  auto it = server_presence_.find(server_id);
  if (it == server_presence_.end()) {
    ServerPresenceState state{};
    // Seed desired from priority-0 default / first priority slot.
    state.desired = rx_timings_.front().conf;
    it = server_presence_.emplace(server_id, state).first;
  }
  return it->second;
}

ServerPresenceState const* ClientConnectivityPolicy::FindServerPresence(
    ServerId server_id) const noexcept {
  auto it = server_presence_.find(server_id);
  return it == server_presence_.end() ? nullptr : &it->second;
}

ServerPresenceState* ClientConnectivityPolicy::FindServerPresence(
    ServerId server_id) noexcept {
  auto it = server_presence_.find(server_id);
  return it == server_presence_.end() ? nullptr : &it->second;
}

void ClientConnectivityPolicy::ConfirmServerPong(ServerId server_id,
                                                 TimePoint send_time,
                                                 TimePoint pong_time,
                                                 Duration interval,
                                                 Duration rx_window,
                                                 Duration selected_rtt) {
  auto& state = EnsureServerPresence(server_id);
  // interval == 0 clears the future Presence promise after the server
  // accepted the reset Ping. rx_window is unrelated to Presence.
  if (interval <= Duration{}) {
    state.has_confirmed_schedule = false;
    state.confirmed_interval = {};
    state.confirmed_rx_window = rx_window;
    state.confirmed_ping_send_time = send_time;
    state.confirmed_pong_receive_time = pong_time;
    state.confirmed_window_open_local = {};
    state.confirmed_window_close_local = {};
    state.config_change_pending = (state.desired.interval != interval) ||
                                  (state.desired.rx_window != rx_window);
    return;
  }
  auto const schedule = MakeConfirmedSchedule(send_time, pong_time, interval,
                                              rx_window, selected_rtt);
  state.has_confirmed_schedule = true;
  state.confirmed_interval = schedule.interval;
  state.confirmed_rx_window = schedule.rx_window;
  state.confirmed_ping_send_time = schedule.ping_send_time;
  state.confirmed_pong_receive_time = schedule.pong_receive_time;
  state.confirmed_window_open_local = schedule.window_open_local;
  state.confirmed_window_close_local = schedule.window_close_local;
  state.config_change_pending = (state.desired.interval != interval) ||
                                (state.desired.rx_window != rx_window);
}

void ClientConnectivityPolicy::ClearServerPresence(ServerId server_id) {
  server_presence_.erase(server_id);
}

void ClientConnectivityPolicy::SetOfflineDetectionTimeout(
    Duration timeout) noexcept {
  if (timeout <= Duration{}) {
    timeout = std::chrono::milliseconds{AE_OFFLINE_DETECTION_TIMEOUT_MS};
  }
  offline_detection_timeout_ = timeout;
}

void ClientConnectivityPolicy::SetCloudRequestExecutionPolicy(
    CloudRequestExecutionPolicy policy) noexcept {
  NormalizeCloudRequestExecutionPolicy(policy);
  cloud_request_execution_policy_ = policy;
}

bool ClientConnectivityPolicy::IsLocallyOnline() const noexcept {
  return IsLocallyOnline(Now());
}

bool ClientConnectivityPolicy::IsLocallyOnline(TimePoint now) const noexcept {
  for (auto const& [id, state] : server_presence_) {
    static_cast<void>(id);
    if (!state.selected_for_aggregate) {
      continue;
    }
    if (IsLocalPresenceOnline(state.has_confirmed_schedule,
                              state.confirmed_interval,
                              state.confirmed_window_open_local, now,
                              offline_detection_timeout_)) {
      return true;
    }
  }
  return false;
}

bool ClientConnectivityPolicy::IsServerLocallyOnline(
    ServerId server_id, TimePoint now) const noexcept {
  auto const* state = FindServerPresence(server_id);
  if (state == nullptr) {
    return false;
  }
  return IsLocalPresenceOnline(state->has_confirmed_schedule,
                               state->confirmed_interval,
                               state->confirmed_window_open_local, now,
                               offline_detection_timeout_);
}

void ClientConnectivityPolicy::ResetRuntimeState() {
  auto current_time = Now();
  for (auto& t : rx_timings_) {
    if (current_time < t.recordet_at) {
      t.next_rx_point = {};
      t.recordet_at = {};
    }
  }
  for (auto& [id, state] : server_presence_) {
    static_cast<void>(id);
    if (current_time < state.confirmed_pong_receive_time) {
      state.has_confirmed_schedule = false;
      state.confirmed_window_open_local = {};
      state.confirmed_window_close_local = {};
    }
  }
}

void ClientConnectivityPolicy::ApplyDesiredIfNoOverride(
    ServerId server_id, ServerPresenceState& state, RxTimingConf conf) {
  if (state.has_user_rx_timing) {
    return;
  }
  auto const timing_changed = (state.desired.interval != conf.interval) ||
                              (state.desired.rx_window != conf.rx_window);
  state.desired = conf;
  if (timing_changed) {
    state.config_change_pending = true;
    server_rx_timing_changed_event_.Emit(server_id);
  }
}

void ClientConnectivityPolicy::ApplyDesiredForAllPriorities(RxTimingConf conf) {
  for (auto& item : rx_timings_) {
    item.conf = conf;
  }
  for (auto& [id, state] : server_presence_) {
    ApplyDesiredIfNoOverride(id, state, conf);
  }
}

void ClientConnectivityPolicy::ApplyDesiredForPriority(std::size_t priority,
                                                       RxTimingConf conf) {
  assert(priority < rx_timings_.size());
  rx_timings_[priority].conf = conf;
  for (auto& [id, state] : server_presence_) {
    if (state.bound_priority != priority) {
      continue;
    }
    ApplyDesiredIfNoOverride(id, state, conf);
  }
}

void ClientConnectivityPolicy::IncrementSuspendBlock() {
  ++suspend_block_count_;
  can_suspend_ = false;
}

void ClientConnectivityPolicy::DecrementSuspendBlock() {
  assert(suspend_block_count_ > 0);
  if (suspend_block_count_ == 0) {
    return;
  }
  --suspend_block_count_;
  can_suspend_ = suspend_block_count_ == 0;
  if (can_suspend_) {
    suspend_allowed_event_.Emit();
  }
}

}  // namespace ae
