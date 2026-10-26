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

#ifndef AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_EXECUTION_POLICY_H_
#define AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_EXECUTION_POLICY_H_

#include <cstddef>
#include <cstdint>

#include <chrono>

#include "ae-numeric/percentile.h"

#include "aether/clock.h"
#include "aether/config.h"

namespace ae {

// Max retries after the initial attempt for one CloudRequest ↔ one server.
// Total attempts = 1 + retry_count ∈ [1, 32].
inline constexpr std::uint8_t kMaxCloudRequestRetryCount{31};

// Runtime (non-wire) CloudRequest latency / retry / hedge policy.
// Orthogonal to RequestPolicy (which servers are candidates).
struct CloudRequestExecutionPolicy {
  // Soft response timeout uses channel response RTT percentile.
  Percentile response_percentile{Percentile::FromPercent(99.0)};
  // Soft-timeout multiplier (1-byte FixedPoint, typically Q2.6).
  TimeoutFactor8 timeout_factor{TimeoutFactor8::FromDouble(1.2)};
  // Retries after the initial attempt. retry_count=0 => 1 attempt total.
  // Clamped to [0, kMaxCloudRequestRetryCount].
  std::uint8_t retry_count{1};
  // How many not-yet-activated following candidates to start on first soft miss.
  std::uint8_t hedge_next_servers{0};

  static constexpr CloudRequestExecutionPolicy Default() noexcept {
    return CloudRequestExecutionPolicy{};
  }

  [[nodiscard]] std::size_t TotalAttempts() const noexcept {
    return static_cast<std::size_t>(retry_count) + 1;
  }

  static constexpr CloudRequestExecutionPolicy FromFactor(
      Percentile percentile, TimeoutFactor8 factor, std::uint8_t retries,
      std::uint8_t hedge) noexcept {
    CloudRequestExecutionPolicy p{};
    p.response_percentile = percentile;
    p.timeout_factor = factor;
    p.retry_count = retries > kMaxCloudRequestRetryCount
                        ? kMaxCloudRequestRetryCount
                        : retries;
    p.hedge_next_servers = hedge;
    return p;
  }
};

inline void NormalizeCloudRequestExecutionPolicy(
    CloudRequestExecutionPolicy& policy) noexcept {
  if (policy.timeout_factor.RawValue() == 0) {
    policy.timeout_factor = TimeoutFactor8::FromDouble(1.0);
  }
  if (policy.retry_count > kMaxCloudRequestRetryCount) {
    policy.retry_count = kMaxCloudRequestRetryCount;
  }
}

// T = round_nearest(rtt_ms * timeout_factor) with FixedPoint scale 2^kScaleExp.
inline Duration ScaleDurationByTimeoutFactor(
    Duration base, TimeoutFactor8 factor) noexcept {
  using Ms = std::chrono::milliseconds;
  auto const base_ms = std::chrono::duration_cast<Ms>(base).count();
  if (base_ms <= 0) {
    return std::chrono::duration_cast<Duration>(Ms{1});
  }
  static_assert(TimeoutFactor8::kScaleExp < 0);
  constexpr int frac_bits = -TimeoutFactor8::kScaleExp;
  constexpr std::int64_t half = std::int64_t{1} << (frac_bits - 1);
  auto const product =
      static_cast<std::int64_t>(base_ms) *
      static_cast<std::int64_t>(factor.RawValue());
  auto const scaled = (product + half) >> frac_bits;
  if (scaled <= 0) {
    return std::chrono::duration_cast<Duration>(Ms{1});
  }
  return std::chrono::duration_cast<Duration>(Ms{scaled});
}

inline Duration ComputeCloudRequestSoftTimeout(
    Duration rtt_percentile,
    CloudRequestExecutionPolicy const& policy) noexcept {
  return ScaleDurationByTimeoutFactor(rtt_percentile, policy.timeout_factor);
}

inline Duration FallbackCloudRequestRtt() noexcept {
  return std::chrono::duration_cast<Duration>(
      std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS});
}

// Per-server execution state used by CloudRequest (unit-testable).
struct CloudRequestServerExecState {
  bool activated{false};
  bool succeeded{false};
  bool exhausted{false};
  // Authenticated API-level error: server is alive; attempt terminal; no
  // no-response quarantine.
  bool remote_error_completed{false};
  bool first_soft_miss_seen{false};
  std::uint8_t attempts_started{0};
  std::uint8_t soft_timeouts{0};
  // Counts OnChannelChanged decisions (one event → one increment).
  std::uint8_t channel_changed_events{0};

  [[nodiscard]] bool IsTerminal() const noexcept {
    return succeeded || exhausted || remote_error_completed;
  }

  [[nodiscard]] bool ShouldSkip() const noexcept {
    return IsTerminal() || !activated;
  }

  [[nodiscard]] bool CanStartAttempt(
      CloudRequestExecutionPolicy const& policy) const noexcept {
    if (!activated || IsTerminal()) {
      return false;
    }
    return attempts_started < policy.TotalAttempts();
  }

  // Start a new attempt. Returns 1-based attempt index, or 0 if not allowed.
  std::uint8_t StartAttempt(CloudRequestExecutionPolicy const& policy) {
    if (!CanStartAttempt(policy)) {
      return 0;
    }
    ++attempts_started;
    return attempts_started;
  }

  enum class SoftTimeoutAction : std::uint8_t {
    kIgnore = 0,
    kRetry,
    kExhaust,
  };

  // Soft timeout for the current in-flight attempt. Does not Restream.
  SoftTimeoutAction OnSoftTimeout(CloudRequestExecutionPolicy const& policy) {
    if (IsTerminal() || !activated) {
      return SoftTimeoutAction::kIgnore;
    }
    ++soft_timeouts;
    first_soft_miss_seen = true;
    if (attempts_started < policy.TotalAttempts()) {
      return SoftTimeoutAction::kRetry;
    }
    exhausted = true;
    return SoftTimeoutAction::kExhaust;
  }

  enum class ChannelChangedAction : std::uint8_t {
    kIgnore = 0,
    kRetry,
    kExhaust,
  };

  // One channel-changed event → at most one LaunchAttempt (or exhaust).
  ChannelChangedAction OnChannelChanged(
      CloudRequestExecutionPolicy const& policy) {
    if (IsTerminal() || !activated) {
      return ChannelChangedAction::kIgnore;
    }
    ++channel_changed_events;
    if (!CanStartAttempt(policy)) {
      exhausted = true;
      return ChannelChangedAction::kExhaust;
    }
    return ChannelChangedAction::kRetry;
  }

  // How many following candidates to activate because of this soft miss.
  [[nodiscard]] std::uint8_t HedgeCountOnThisMiss(
      CloudRequestExecutionPolicy const& policy) const noexcept {
    // Hedge only on the first soft miss of this server.
    if (soft_timeouts != 1) {
      return 0;
    }
    return policy.hedge_next_servers;
  }

  void MarkSucceeded() {
    if (exhausted || remote_error_completed) {
      return;
    }
    succeeded = true;
  }

  // Valid authenticated response with API-level failure — server is alive.
  void MarkRemoteErrorCompleted() {
    if (succeeded || exhausted) {
      return;
    }
    remote_error_completed = true;
  }

  void MarkExhausted() { exhausted = true; }
};

inline bool CloudRequestShouldFailAll(bool any_open,
                                      bool any_succeeded) noexcept {
  return !any_open && !any_succeeded;
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_CLOUD_REQUEST_EXECUTION_POLICY_H_
