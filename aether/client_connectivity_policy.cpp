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

#include "aether/ae_context.h"

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

ClientConnectivityPolicy::ClientConnectivityPolicy()
    : rx_targets_{RequestPolicy::All{}}, rx_timings_{MakeDefaultRxTimings()} {}

#ifdef AE_DISTILLATION
ClientConnectivityPolicy::ClientConnectivityPolicy(ObjProp prop)
    : Base{prop},
      connectivity_policy_{
          std::make_unique<ConnectivityPolicy>(AeContext{*this})} {
  // set state from the policy
  rx_targets_ = connectivity_policy_->rx_targets();
  rx_timings_ = connectivity_policy_->rx_timings();
  server_presence_ = connectivity_policy_->server_presence();

  connectivity_policy_->state_changed().Subscribe([&]() {
    // update state from the policy
    rx_targets_ = connectivity_policy_->rx_targets();
    rx_timings_ = connectivity_policy_->rx_timings();
    server_presence_ = connectivity_policy_->server_presence();
  });
}
#endif

void ClientConnectivityPolicy::Loaded() {
  // make policy with loaded state
  connectivity_policy_ = std::make_unique<ConnectivityPolicy>(
      AeContext{*this}, rx_targets_, rx_timings_, server_presence_);
  // make state actual according to policy
  rx_targets_ = connectivity_policy_->rx_targets();
  rx_timings_ = connectivity_policy_->rx_timings();
  server_presence_ = connectivity_policy_->server_presence();

  connectivity_policy_->state_changed().Subscribe([&]() {
    // update state from the policy
    rx_targets_ = connectivity_policy_->rx_targets();
    rx_targets_ = connectivity_policy_->rx_targets();
    server_presence_ = connectivity_policy_->server_presence();
  });
}

auto ClientConnectivityPolicy::ConfigureRxTimings(
    RequestPolicy::Variant targets) -> RxTimingConfig {
  return connectivity_policy_->ConfigureRxTimings(targets);
}

void ClientConnectivityPolicy::ConfigureServerRxTiming(
    ServerId server_id, RxTimingConf conf,
    Percentile rtt_reliability_percentile) {
  connectivity_policy_->ConfigureServerRxTiming(server_id, conf,
                                                rtt_reliability_percentile);
}

auto ClientConnectivityPolicy::suspend_allowed_event() noexcept
    -> Event<void()> const& {
  return connectivity_policy_->suspend_allowed_event();
}

ConnectivityStatus ClientConnectivityPolicy::GetStatus() const noexcept {
  return connectivity_policy_->GetStatus();
}

void ClientConnectivityPolicy::ResetRxTimings() {
  connectivity_policy_->ResetRxTimings();
  for (auto& t : rx_timings_) {
    t.next_rx_point = {};
    t.recordet_at = {};
  }
}
}  // namespace ae
