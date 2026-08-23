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

// Local RX capability closes at pong/timeout receive + receive_window
// (not at ping send + receive_window).
inline TimePoint ComputeRxWindowCloseTime(TimePoint receive_or_timeout_time,
                                          Duration receive_window) noexcept {
  return receive_or_timeout_time + receive_window;
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_PING_SCHEDULE_GUARD_H_
