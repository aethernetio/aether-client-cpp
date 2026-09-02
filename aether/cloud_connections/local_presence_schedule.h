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

namespace ae {

// Fixed scheduler safety guard (not rx_window).
inline constexpr Duration kLocalPresenceGuard =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{30});

inline constexpr std::uint8_t kDefaultRttReliabilityPercentile{99};

enum class PingAttemptKind : std::uint8_t {
  kInitial = 0,
  kPrefix1,
  kPrefix2,
  kRetry,
  kRecovery,
};

// One-way RTT projection used consistently for schedule placement.
// Documented model: one_way = rtt / 2 (local monotonic timeline only).
inline Duration OneWayFromRtt(Duration rtt) noexcept {
  return rtt / 2;
}

struct ConfirmedReceiveSchedule {
  Duration interval{};
  Duration rx_window{};
  TimePoint ping_send_time{};
  TimePoint pong_receive_time{};
  Duration measured_rtt{};
  TimePoint window_open_local{};
  TimePoint window_close_local{};
};

// After successful Pong for Ping sent at send_time:
//   R_server ≈ send_time + one_way
//   window_open  = R_server + interval
//   window_close = window_open + rx_window
inline ConfirmedReceiveSchedule MakeConfirmedSchedule(
    TimePoint send_time, TimePoint pong_time, Duration interval,
    Duration rx_window) noexcept {
  ConfirmedReceiveSchedule out{};
  out.interval = interval;
  out.rx_window = rx_window;
  out.ping_send_time = send_time;
  out.pong_receive_time = pong_time;
  if (pong_time > send_time) {
    out.measured_rtt =
        std::chrono::duration_cast<Duration>(pong_time - send_time);
  }
  auto const one_way = OneWayFromRtt(out.measured_rtt);
  out.window_open_local = send_time + one_way + interval;
  out.window_close_local = out.window_open_local + rx_window;
  return out;
}

// prefix1 = O - 1.5*R - G
// prefix2 = O - 0.5*R - G
inline TimePoint ComputePrefix1Time(TimePoint window_open, Duration rtt,
                                    Duration guard = kLocalPresenceGuard) noexcept {
  return window_open - (rtt * 3) / 2 - guard;
}

inline TimePoint ComputePrefix2Time(TimePoint window_open, Duration rtt,
                                    Duration guard = kLocalPresenceGuard) noexcept {
  return window_open - rtt / 2 - guard;
}

inline bool IsConfirmedWindowOnline(bool has_confirmed, TimePoint now,
                                    TimePoint window_close) noexcept {
  if (!has_confirmed) {
    return false;
  }
  return now <= window_close;
}

inline bool IsCurrentPingAttempt(std::uint64_t active_attempt_id,
                                 std::uint64_t result_attempt_id) noexcept {
  return active_attempt_id == result_attempt_id;
}

// Next Ping after a failed attempt. p99/selected RTT timeout is NOT OFFLINE.
struct PresenceAttemptPlan {
  TimePoint when{};
  PingAttemptKind kind{PingAttemptKind::kRecovery};
  bool mark_offline{false};
};

inline PresenceAttemptPlan PlanAfterFailedAttempt(
    bool has_confirmed_schedule, TimePoint confirmed_window_open,
    TimePoint confirmed_window_close, PingAttemptKind failed_kind,
    TimePoint now, Duration rtt,
    Duration guard = kLocalPresenceGuard) noexcept {
  PresenceAttemptPlan plan{};
  if (has_confirmed_schedule && now <= confirmed_window_close) {
    plan.mark_offline = false;
    if (failed_kind == PingAttemptKind::kPrefix1) {
      plan.kind = PingAttemptKind::kPrefix2;
      auto const prefix2 =
          ComputePrefix2Time(confirmed_window_open, rtt, guard);
      plan.when = prefix2 > now ? prefix2 : now;
      return plan;
    }
    plan.kind = PingAttemptKind::kRetry;
    plan.when = now + rtt;
    return plan;
  }
  plan.mark_offline = has_confirmed_schedule;
  plan.kind = PingAttemptKind::kRecovery;
  plan.when = now + rtt;
  return plan;
}

// After a confirming Pong: prefix1 of the new window, unless a newer desired
// interval/window still needs a Ping.
inline PresenceAttemptPlan PlanAfterSuccessfulPong(
    TimePoint confirmed_window_open, TimePoint now, Duration rtt,
    bool send_new_config_immediately,
    Duration guard = kLocalPresenceGuard) noexcept {
  PresenceAttemptPlan plan{};
  plan.mark_offline = false;
  if (send_new_config_immediately) {
    plan.kind = PingAttemptKind::kInitial;
    plan.when = now;
    return plan;
  }
  plan.kind = PingAttemptKind::kPrefix1;
  auto const prefix1 = ComputePrefix1Time(confirmed_window_open, rtt, guard);
  plan.when = prefix1 > now ? prefix1 : now;
  return plan;
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_LOCAL_PRESENCE_SCHEDULE_H_
