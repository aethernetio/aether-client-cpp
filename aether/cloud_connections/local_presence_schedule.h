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

#ifndef AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_SCHEDULE_H_
#define AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_SCHEDULE_H_

#include <chrono>
#include <cstdint>

#include "aether/clock.h"
#include "ae-numeric/percentile.h"

namespace ae {

// Fixed scheduler safety guard (not rx_window).
inline constexpr Duration kLocalPresenceGuard =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{30});

inline constexpr Percentile kDefaultRttReliabilityPercentile =
    Percentile::FromPercent(99.0);

inline constexpr std::size_t kMaxOutstandingPresenceAttempts{8};

enum class PingAttemptKind : std::uint8_t {
  kInitial = 0,
  kPrefix1,
  kPrefix2,
  kRetry,
  kRecovery,
};

enum class PresenceRestreamReason : std::uint8_t {
  kNone = 0,
  kHardWriteFailure,
  kHardLinkFailure,
  kPingApiError,
  kConnectionUnavailable,
};

// One-way RTT projection used consistently for schedule placement.
// Documented model: one_way = selected_rtt / 2 (local monotonic timeline only).
inline Duration OneWayFromRtt(Duration rtt) noexcept { return rtt / 2; }

inline Duration MaxDuration(Duration a, Duration b) noexcept {
  return a > b ? a : b;
}

inline Duration PresenceHardWait(Duration selected_rtt, Duration interval,
                                 Duration window) noexcept {
  return MaxDuration(selected_rtt * 8, interval + window);
}

struct ConfirmedReceiveSchedule {
  Duration interval{};
  Duration rx_window{};
  TimePoint ping_send_time{};
  TimePoint pong_receive_time{};
  Duration measured_rtt{};
  Duration selected_rtt{};
  TimePoint window_open_local{};
  TimePoint window_close_local{};
};

// After successful Pong for Ping sent at send_time:
//   estimated_server_receive = send_time + selected_rtt / 2
//   window_open  = estimated_server_receive + sent_interval
//   window_close = window_open + sent_window
// measured RTT is diagnostics/statistics only and must not move the projection.
inline ConfirmedReceiveSchedule MakeConfirmedSchedule(
    TimePoint send_time, TimePoint pong_time, Duration interval,
    Duration rx_window, Duration selected_rtt) noexcept {
  ConfirmedReceiveSchedule out{};
  out.interval = interval;
  out.rx_window = rx_window;
  out.ping_send_time = send_time;
  out.pong_receive_time = pong_time;
  out.selected_rtt = selected_rtt;
  if (pong_time > send_time) {
    out.measured_rtt =
        std::chrono::duration_cast<Duration>(pong_time - send_time);
  }
  auto const one_way = OneWayFromRtt(selected_rtt);
  out.window_open_local = send_time + one_way + interval;
  out.window_close_local = out.window_open_local + rx_window;
  return out;
}

struct CadencePlan {
  TimePoint following_open_target{};
  Duration wire_interval{};
};

// configured interval is the distance between planned opening targets, not
// between early prefix sends.
inline CadencePlan PlanWireInterval(TimePoint send_time, Duration selected_rtt,
                                    TimePoint following_open_target,
                                    Duration desired_interval) noexcept {
  auto const a_estimated = send_time + OneWayFromRtt(selected_rtt);
  if (following_open_target <= a_estimated) {
    return CadencePlan{a_estimated + desired_interval, desired_interval};
  }
  return CadencePlan{
      following_open_target,
      std::chrono::duration_cast<Duration>(following_open_target -
                                           a_estimated)};
}

// prefix1 = O - 1.5*R - G
// prefix2 = O - 0.5*R - G
inline TimePoint ComputePrefix1Time(
    TimePoint window_open, Duration rtt,
    Duration guard = kLocalPresenceGuard) noexcept {
  return window_open - (rtt * 3) / 2 - guard;
}

inline TimePoint ComputePrefix2Time(
    TimePoint window_open, Duration rtt,
    Duration guard = kLocalPresenceGuard) noexcept {
  return window_open - rtt / 2 - guard;
}

// Local Presence ONLINE when a confirmed future opening exists and now is
// still within expected_open + offline_detection_timeout.
// rx_window / confirmed_window_close are NOT used for Presence.
inline bool IsLocalPresenceOnline(bool has_confirmed, Duration confirmed_interval,
                                  TimePoint expected_open, TimePoint now,
                                  Duration offline_detection_timeout) noexcept {
  if (!has_confirmed) {
    return false;
  }
  if (confirmed_interval <= Duration{}) {
    return false;
  }
  return now <= (expected_open + offline_detection_timeout);
}

inline TimePoint LocalOfflineDeadline(
    TimePoint expected_open, Duration offline_detection_timeout) noexcept {
  return expected_open + offline_detection_timeout;
}

// Deprecated name kept for transitional call sites that still pass close.
// Prefer IsLocalPresenceOnline.
inline bool IsConfirmedWindowOnline(bool has_confirmed, TimePoint now,
                                    TimePoint window_close) noexcept {
  if (!has_confirmed) {
    return false;
  }
  return now <= window_close;
}

inline bool AttemptOpensPromisedWindow(PingAttemptKind kind) noexcept {
  return kind == PingAttemptKind::kPrefix1 ||
         kind == PingAttemptKind::kPrefix2 || kind == PingAttemptKind::kRetry;
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_SCHEDULE_H_
