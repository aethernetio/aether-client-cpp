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

#include "aether/cloud_connections/ping_schedule_guard.h"

namespace ae {

namespace {
constexpr auto kDefaultTiming = RxTiming{
    .conf = RxTimingConf::Every(std::chrono::milliseconds{AE_PING_INTERVAL_MS}),
    .next_rx_point = {},
    .recordet_at = {}};;

Duration AgeSince(TimePoint now, TimePoint then) noexcept {
  if (then == TimePoint{}) {
    return Duration{};
  }
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

void ClientConnectivityPolicy::ResetRxTimings() {
  for (auto& t : rx_timings_) {
    t.next_rx_point = {};
    t.recordet_at = {};
  }
  last_successful_cloud_response_ = {};
  local_online_until_ = {};
  pending_ping_deadline_ = {};
  pings_in_flight_ = 0;
}

void ClientConnectivityPolicy::ReportSuccessfulCloudResponse(TimePoint at) {
  last_successful_cloud_response_ = at;
  auto const window = MaxReceiveWindow();
  local_online_until_ = ComputeRxWindowCloseTime(at, window);
  if (pings_in_flight_ > 0) {
    --pings_in_flight_;
  }
  if (pings_in_flight_ == 0) {
    pending_ping_deadline_ = {};
  }
}

void ClientConnectivityPolicy::ReportPingDispatched(
    TimePoint send_time, Duration response_timeout) {
  ++pings_in_flight_;
  auto const resolve_at = send_time + response_timeout;
  if (pending_ping_deadline_ == TimePoint{} ||
      resolve_at > pending_ping_deadline_) {
    pending_ping_deadline_ = resolve_at;
  }
}

void ClientConnectivityPolicy::ReportPingCompletedWithoutSuccess(TimePoint at) {
  if (pings_in_flight_ > 0) {
    --pings_in_flight_;
  }
  if (pings_in_flight_ == 0) {
    pending_ping_deadline_ = {};
  }
}

Duration ClientConnectivityPolicy::MaxReceiveWindow() const noexcept {
  Duration window{};
  for (auto const& t : rx_timings_) {
    if (t.conf.rx_window > window) {
      window = t.conf.rx_window;
    }
  }
  return window;
}

bool ClientConnectivityPolicy::IsLocallyOnline(TimePoint now) const noexcept {
  auto const window = MaxReceiveWindow();
  if (local_online_until_ != TimePoint{} && now < local_online_until_) {
    return true;
  }
  if (pings_in_flight_ > 0 && pending_ping_deadline_ != TimePoint{} &&
      now < pending_ping_deadline_) {
    return true;
  }
  return false;
}

LocalConnectivitySnapshot ClientConnectivityPolicy::InspectLocalConnectivity(
    TimePoint now) const noexcept {
  auto const window = MaxReceiveWindow();
  auto const age = AgeSince(now, last_successful_cloud_response_);
  bool const online = IsLocallyOnline(now);
  LocalConnectivitySnapshot snapshot{
      .now = now,
      .last_success = last_successful_cloud_response_,
      .local_online_until = local_online_until_,
      .pending_ping_deadline = pending_ping_deadline_,
      .pings_in_flight = pings_in_flight_,
      .receive_window = window,
      .age = age,
      .online = online,
  };
  return snapshot;
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
    // if clock was reset, also reset next rx points
    if (current_time < t.recordet_at) {
      t.next_rx_point = {};
      t.recordet_at = {};
    }
  }
  if (current_time < last_successful_cloud_response_) {
    last_successful_cloud_response_ = {};
    local_online_until_ = {};
    pending_ping_deadline_ = {};
    pings_in_flight_ = 0;
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
