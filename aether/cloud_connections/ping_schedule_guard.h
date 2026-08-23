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

#ifndef AETHER_CLOUD_CONNECTIONS_PING_SCHEDULE_GUARD_H_
#define AETHER_CLOUD_CONNECTIONS_PING_SCHEDULE_GUARD_H_

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

#include "aether/clock.h"

namespace ae {

inline constexpr Duration kPingGuardFloor =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{10});

// guard = max(0, (p99 - min) / 2) + 10ms
inline Duration ComputePingSendGuard(Duration min_rtt,
                                     Duration p99_rtt) noexcept {
  Duration spread{};
  if (p99_rtt > min_rtt) {
    spread = (p99_rtt - min_rtt) / 2;
  }
  return spread + kPingGuardFloor;
}

// Guard cannot be >= ping_interval; leave at least 1ms before nominal interval.
inline Duration ClampPingSendGuard(Duration guard,
                                   Duration ping_interval) noexcept {
  auto const min_remaining =
      std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1});
  if (ping_interval <= min_remaining) {
    return Duration{};
  }
  auto const max_guard = ping_interval - min_remaining;
  return guard > max_guard ? max_guard : guard;
}

// Empty stats: min = p99 = 200ms → guard = 10ms.
// One sample: min == p99 → guard = 10ms.
template <typename Stats>
inline Duration ComputePingSendGuardFromStats(
    Stats const& stats, Duration ping_interval) noexcept {
  auto const estimate =
      std::chrono::duration_cast<Duration>(std::chrono::milliseconds{200});
  Duration min_rtt = estimate;
  Duration p99_rtt = estimate;
  if (!stats.empty()) {
    min_rtt = stats.min();
    p99_rtt = stats.template percentile<99>();
  }
  return ClampPingSendGuard(ComputePingSendGuard(min_rtt, p99_rtt),
                            ping_interval);
}

inline Duration SaturatingAddDuration(Duration a, Duration b) noexcept {
  auto const max = Duration::max();
  if (b > max - a) {
    return max;
  }
  return a + b;
}

inline TimePoint SaturatingAddTime(TimePoint t, Duration d) noexcept {
  if (d.count() == 0) {
    return t;
  }
  using Tick = typename TimePoint::duration;
  auto const add = std::chrono::duration_cast<Tick>(d);
  auto const max_t = TimePoint::max();
  if (t >= max_t) {
    return max_t;
  }
  if (add > max_t - t) {
    return max_t;
  }
  return t + add;
}

// Positive later-earlier as Duration; zero if later <= earlier. Saturates.
inline Duration SaturatingSubTime(TimePoint later, TimePoint earlier) noexcept {
  if (later <= earlier) {
    return Duration{};
  }
  auto const delta = later - earlier;
  auto const us =
      std::chrono::duration_cast<std::chrono::microseconds>(delta);
  if (us.count() <= 0) {
    return Duration{};
  }
  auto const max_us = static_cast<std::int64_t>(
      std::numeric_limits<typename Duration::rep>::max());
  if (us.count() >= max_us) {
    return Duration::max();
  }
  return Duration{static_cast<typename Duration::rep>(us.count())};
}

inline std::int64_t DurationToSaturatedInt64Ms(Duration d) noexcept {
  if (d.count() <= 0) {
    return 0;
  }
  auto const us =
      std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  if (us <= 0) {
    return std::numeric_limits<std::int64_t>::max();
  }
  constexpr auto max_ms = std::numeric_limits<std::int64_t>::max();
  if (us > max_ms - 999) {
    return max_ms;
  }
  return static_cast<std::int64_t>(us / 1000);
}

struct EarlyRxWindowInput {
  bool has_planned_send{false};
  TimePoint planned_send_at{};
  TimePoint actual_send_at{};
  Duration base_rx_window{};
  bool has_required_rx_until{false};
  TimePoint required_rx_until{};
};

struct EarlyRxWindowOutput {
  Duration early_by{};
  Duration effective_wire_rx_window{};
  TimePoint required_rx_until{};
};

// planned/actual/required-end math for one ping attempt. Does not use RTT.
inline EarlyRxWindowOutput ComputeEarlyRxWindow(
    EarlyRxWindowInput const& in) noexcept {
  EarlyRxWindowOutput out{};
  if (in.has_planned_send && in.planned_send_at > in.actual_send_at) {
    out.early_by = SaturatingSubTime(in.planned_send_at, in.actual_send_at);
  }

  auto raise = [&](TimePoint candidate) {
    if (out.required_rx_until == TimePoint{} ||
        candidate > out.required_rx_until) {
      out.required_rx_until = candidate;
    }
  };

  if (in.has_required_rx_until) {
    raise(in.required_rx_until);
  }
  raise(SaturatingAddTime(in.actual_send_at, in.base_rx_window));
  if (in.has_planned_send) {
    raise(SaturatingAddTime(in.planned_send_at, in.base_rx_window));
  }

  out.effective_wire_rx_window =
      SaturatingSubTime(out.required_rx_until, in.actual_send_at);
  if (in.base_rx_window.count() > 0 &&
      out.effective_wire_rx_window < in.base_rx_window) {
    out.effective_wire_rx_window = in.base_rx_window;
    raise(SaturatingAddTime(in.actual_send_at, in.base_rx_window));
  }
  return out;
}

struct LocalRxWindowState {
  bool open{false};
  TimePoint close_at{};
  std::uint64_t generation{0};
};

// Returns true if the close timer must be (re)scheduled. Never shortens.
inline bool ExtendLocalRxUntil(LocalRxWindowState& state,
                               TimePoint close_at) noexcept {
  if (state.open && close_at <= state.close_at) {
    return false;
  }
  state.open = true;
  state.close_at = close_at;
  ++state.generation;
  return true;
}

inline bool ShouldApplyCloseTimer(LocalRxWindowState const& state,
                                  std::uint64_t generation,
                                  TimePoint now) noexcept {
  return state.open && state.generation == generation && now >= state.close_at;
}

inline void CloseLocalRx(LocalRxWindowState& state) noexcept {
  state.open = false;
}

// Write failure before the ping is recorded must not close a still-open window.
inline bool ShouldCloseLocalRxAfterWriteFailure(
    LocalRxWindowState const& state, bool has_required_until,
    TimePoint required_until, TimePoint now) noexcept {
  bool const previous_active =
      (has_required_until && required_until > now) ||
      (state.open && state.close_at > now);
  return !previous_active;
}

// Local RX capability closes at pong/timeout receive + receive_window
// (not at ping send + receive_window).
inline TimePoint ComputeRxWindowCloseTime(TimePoint receive_or_timeout_time,
                                          Duration receive_window) noexcept {
  return SaturatingAddTime(receive_or_timeout_time, receive_window);
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_PING_SCHEDULE_GUARD_H_
