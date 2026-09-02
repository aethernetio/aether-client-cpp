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

namespace ae {

namespace {
constexpr auto kDefaultTiming = RxTiming{
    .conf = RxTimingConf::Every(std::chrono::milliseconds{AE_PING_INTERVAL_MS}),
    .next_rx_point = {},
    .recordet_at = {}};;

Duration AgeSince(TimePoint now, TimePoint then) noexcept {
  if (now <= then) {
    return Duration{};
  }
  return std::chrono::duration_cast<Duration>(now - then);
}

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
  for (auto& item : policy_->rx_timings_) {
    item.conf = conf;
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
  return ConnectivityStatus{.can_suspend = can_suspend_,
                            .suspend_block_count = suspend_block_count_,
                            .next_service_time = next_service_time};
}

void ClientConnectivityPolicy::ClearPingFlight(
    std::size_t priority) noexcept {
  assert(priority < ping_flights_.size());
  ping_flights_[priority] = {};
}

void ClientConnectivityPolicy::ClearAllLocalConnectivityState() noexcept {
  has_successful_cloud_response_ = false;
  last_successful_cloud_response_ = {};
  last_success_ping_interval_ = {};
  ping_flights_.fill({});
}

void ClientConnectivityPolicy::ResetRxTimings() {
  for (auto& t : rx_timings_) {
    t.next_rx_point = {};
    t.recordet_at = {};
  }
  ClearAllLocalConnectivityState();
}

void ClientConnectivityPolicy::ReportSuccessfulCloudResponse(
    TimePoint at, Duration ping_interval, std::size_t priority) {
  assert(priority < ping_flights_.size());
  has_successful_cloud_response_ = true;
  last_successful_cloud_response_ = at;
  if (ping_interval > Duration{}) {
    last_success_ping_interval_ = ping_interval;
  }
  ClearPingFlight(priority);
}

bool ClientConnectivityPolicy::WasBridgingOnlineAt(
    TimePoint when, std::size_t skip_priority) const noexcept {
  if (HasRecentCloudResponse(when)) {
    return true;
  }
  for (std::size_t i = 0; i < ping_flights_.size(); ++i) {
    if (i == skip_priority) {
      continue;
    }
    auto const& flight = ping_flights_[i];
    if (flight.in_flight && flight.bridges_online && when < flight.deadline) {
      return true;
    }
  }
  return false;
}

void ClientConnectivityPolicy::ReportPingDispatched(
    TimePoint send_time, Duration response_timeout, std::size_t priority) {
  assert(priority < ping_flights_.size());
  auto& flight = ping_flights_[priority];
  flight.in_flight = true;
  flight.deadline = send_time + response_timeout;
  flight.bridges_online = WasBridgingOnlineAt(send_time, priority);
}

void ClientConnectivityPolicy::ReportPingCompletedWithoutSuccess(
    TimePoint at, std::size_t priority) {
  (void)at;
  assert(priority < ping_flights_.size());
  ClearPingFlight(priority);
}

bool ClientConnectivityPolicy::HasRecentCloudResponse(
    TimePoint now) const noexcept {
  if (!has_successful_cloud_response_) {
    return false;
  }
  if (last_success_ping_interval_ <= Duration{}) {
    return false;
  }
  return AgeSince(now, last_successful_cloud_response_) <
         last_success_ping_interval_;
}

bool ClientConnectivityPolicy::HasActiveInFlightGrace(
    TimePoint now) const noexcept {
  for (auto const& flight : ping_flights_) {
    if (flight.in_flight && flight.bridges_online && now < flight.deadline) {
      return true;
    }
  }
  return false;
}

bool ClientConnectivityPolicy::IsLocallyOnline(TimePoint now) const noexcept {
  return HasRecentCloudResponse(now) || HasActiveInFlightGrace(now);
}

std::optional<Duration>
ClientConnectivityPolicy::TimeSinceLastSuccessfulCloudResponse(
    TimePoint now) const noexcept {
  if (!has_successful_cloud_response_) {
    return std::nullopt;
  }
  return AgeSince(now, last_successful_cloud_response_);
}

LocalConnectivitySnapshot ClientConnectivityPolicy::InspectLocalConnectivity(
    TimePoint now) const noexcept {
  auto const age = AgeSince(now, last_successful_cloud_response_);
  bool const has_success = has_successful_cloud_response_;
  TimePoint recent_success_until{};
  if (has_success && last_success_ping_interval_ > Duration{}) {
    recent_success_until =
        last_successful_cloud_response_ + last_success_ping_interval_;
  }

  std::uint32_t in_flight_count = 0;
  TimePoint pending_deadline{};
  bool grace_active = false;
  for (auto const& flight : ping_flights_) {
    if (!flight.in_flight) {
      continue;
    }
    ++in_flight_count;
    if (flight.bridges_online && now < flight.deadline) {
      grace_active = true;
      if (pending_deadline == TimePoint{} ||
          flight.deadline > pending_deadline) {
        pending_deadline = flight.deadline;
      }
    }
  }

  return LocalConnectivitySnapshot{
      .now = now,
      .has_success = has_success,
      .age_since_last_success = age,
      .ping_interval = last_success_ping_interval_,
      .last_success = last_successful_cloud_response_,
      .recent_success_until = recent_success_until,
      .pings_in_flight = in_flight_count,
      .pending_ping_deadline = pending_deadline,
      .in_flight_grace_active = grace_active,
      .online = IsLocallyOnline(now),
  };
}

void ClientConnectivityPolicy::ReportNextServiceTime(
    std::size_t priority, TimePoint next_service_time) {
  assert(priority < rx_timings_.size() && "Invalid priority value");

  auto& t = rx_timings_.at(priority);
  t.next_rx_point = next_service_time;
  t.recordet_at = Now();
}

void ClientConnectivityPolicy::ResetRuntimeState() {
  auto current_time = Now();
  for (auto& t : rx_timings_) {
    if (current_time < t.recordet_at) {
      t.next_rx_point = {};
      t.recordet_at = {};
    }
  }
  if (has_successful_cloud_response_ &&
      current_time < last_successful_cloud_response_) {
    ClearAllLocalConnectivityState();
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
