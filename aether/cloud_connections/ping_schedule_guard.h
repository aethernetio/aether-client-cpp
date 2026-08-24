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

inline TimePoint SaturatingSubDuration(TimePoint t, Duration d) noexcept {
  if (d.count() == 0) {
    return t;
  }
  using Tick = typename TimePoint::duration;
  using Rep = typename Tick::rep;
  using URep = std::make_unsigned_t<Rep>;
  auto const sub = std::chrono::duration_cast<Tick>(d);
  if (sub.count() <= 0) {
    return d.count() > 0 ? TimePoint::min() : t;
  }
  auto const t_count = t.time_since_epoch().count();
  auto const sub_count = sub.count();
  auto const min_count = TimePoint::min().time_since_epoch().count();
  auto const room = static_cast<URep>(t_count) - static_cast<URep>(min_count);
  if (static_cast<URep>(sub_count) > room) {
    return TimePoint::min();
  }
  return t - sub;
}

// Floor remaining to whole milliseconds. Known schedules never wire 0.
inline std::int64_t FloorDurationToPositiveInt64Ms(Duration d) noexcept {
  if (d.count() <= 0) {
    return 1;
  }
  auto const us =
      std::chrono::duration_cast<std::chrono::microseconds>(d).count();
  if (us <= 0) {
    return 1;
  }
  constexpr auto max_ms = std::numeric_limits<std::int64_t>::max();
  if (us / 1000 >= max_ms) {
    return max_ms;
  }
  auto const ms = static_cast<std::int64_t>(us / 1000);
  return ms < 1 ? 1 : ms;
}

// Ceil to whole milliseconds so the server RX window is not shorter.
inline std::int64_t CeilDurationToSaturatedInt64Ms(Duration d) noexcept {
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
  return static_cast<std::int64_t>((us + 999) / 1000);
}

inline TimePoint SaturatingAddTicks(
    TimePoint t, typename TimePoint::duration add) noexcept {
  auto const max_t = TimePoint::max();
  if (add.count() <= 0) {
    return t;
  }
  if (t >= max_t) {
    return max_t;
  }
  if (add > max_t - t) {
    return max_t;
  }
  return t + add;
}

// Advance deadline by whole intervals until it is strictly after retry_actual.
// Uses ceil-style division; never iterates once per interval.
inline TimePoint AdvanceContractDeadlinePast(TimePoint deadline,
                                            TimePoint retry_actual,
                                            Duration interval) noexcept {
  if (retry_actual < deadline) {
    return deadline;
  }
  using Tick = typename TimePoint::duration;
  auto interval_ticks = std::chrono::duration_cast<Tick>(interval);
  if (interval_ticks.count() <= 0) {
    interval_ticks =
        std::chrono::duration_cast<Tick>(std::chrono::milliseconds{1});
  }
  if (interval_ticks.count() <= 0) {
    return TimePoint::max();
  }
  auto const elapsed =
      retry_actual >= deadline ? (retry_actual - deadline) : Tick{};
  auto const iv = interval_ticks.count();
  auto const el = elapsed.count();
  auto const max_rep = std::numeric_limits<typename Tick::rep>::max();
  typename Tick::rep n = 1;
  if (el > 0) {
    if (el / iv > max_rep - 1) {
      return TimePoint::max();
    }
    n = el / iv + 1;
  }
  if (iv > 0 && n > max_rep / iv) {
    return TimePoint::max();
  }
  return SaturatingAddTicks(deadline, Tick{iv * n});
}

enum class PingErrorRetryAction : std::uint8_t {
  kImmediateSameCycle = 0,
  kRestreamThenSameCycle = 1,
};

inline PingErrorRetryAction PingErrorRetryActionFor(int error_code) noexcept {
  if (error_code == 2) {
    return PingErrorRetryAction::kImmediateSameCycle;
  }
  return PingErrorRetryAction::kRestreamThenSameCycle;
}

inline bool ShouldAcceptCycleResult(bool cycle_active, bool cycle_confirmed,
                                    std::uint32_t current_attempt,
                                    std::uint32_t result_attempt,
                                    bool current_attempt_timed_out,
                                    bool result_is_ok_or_late) noexcept {
  if (!cycle_active || cycle_confirmed) {
    return false;
  }
  if (result_attempt != current_attempt) {
    return false;
  }
  // Same-attempt timeout still accepts a late pong so we can cancel the
  // retry. A response from an older attempt is rejected above.
  (void)current_attempt_timed_out;
  (void)result_is_ok_or_late;
  return true;
}

struct LogicalPingCycleState {
  std::uint64_t cycle_id{0};
  bool active{false};
  bool confirmed{false};
  bool has_schedule{false};
  bool current_attempt_timed_out{false};
  bool awaiting_relink_retry{false};
  std::uint32_t attempt_index{0};
  TimePoint first_attempt_send_at{};
  TimePoint cycle_anchor{};
  TimePoint contract_deadline_at{};
  TimePoint next_local_send_at{};
  TimePoint planned_send_at{};
  TimePoint required_rx_until{};
  Duration configured_interval{};
  Duration base_rx_window{};
  Duration current_guard{};
};

struct LogicalPingAttemptRequest {
  TimePoint actual_send_at{};
  bool has_planned_send{false};
  TimePoint planned_send_at{};
  Duration interval{};
  Duration guard{};
  Duration base_rx_window{};
};

struct LogicalPingAttemptView {
  std::uint64_t cycle_id{0};
  std::uint32_t attempt_index{0};
  bool is_retry{false};
  bool started_new_cycle{false};
  TimePoint cycle_anchor{};
  TimePoint contract_deadline{};
  TimePoint next_local_send{};
  Duration wire_next_connect{};
  std::int64_t wire_next_connect_ms{0};
  EarlyRxWindowOutput rx{};
};

inline LogicalPingAttemptView ApplyLogicalPingAttempt(
    LogicalPingCycleState& st, LogicalPingAttemptRequest const& req) noexcept {
  LogicalPingAttemptView view{};
  bool const new_cycle = !st.active || st.confirmed;
  view.started_new_cycle = new_cycle;
  view.is_retry = !new_cycle;

  auto const guard = ClampPingSendGuard(req.guard, req.interval);

  if (new_cycle) {
    st.cycle_id += 1;
    if (st.cycle_id == 0) {
      st.cycle_id = 1;
    }
    st.active = true;
    st.confirmed = false;
    st.current_attempt_timed_out = false;
    st.awaiting_relink_retry = false;
    st.attempt_index = 1;
    st.first_attempt_send_at = req.actual_send_at;
    st.configured_interval = req.interval;
    st.base_rx_window = req.base_rx_window;
    st.current_guard = guard;
    st.planned_send_at =
        req.has_planned_send ? req.planned_send_at : req.actual_send_at;

    // next_local_send is guard-early. A send after that instant but still
    // before contract_deadline is on time for this slot; only missing the
    // deadline advances the cadence.
    bool const late_new_cycle =
        st.has_schedule && req.actual_send_at > st.contract_deadline_at;
    if (late_new_cycle) {
      auto next_slot = SaturatingAddTime(st.contract_deadline_at, req.interval);
      if (req.actual_send_at >= next_slot) {
        next_slot = AdvanceContractDeadlinePast(next_slot, req.actual_send_at,
                                                req.interval);
      }
      st.contract_deadline_at = next_slot;
      st.cycle_anchor =
          SaturatingSubDuration(st.contract_deadline_at, req.interval);
      st.next_local_send_at =
          SaturatingSubDuration(st.contract_deadline_at, guard);
      view.wire_next_connect =
          SaturatingSubTime(st.contract_deadline_at, req.actual_send_at);
    } else {
      st.cycle_anchor = req.actual_send_at;
      st.contract_deadline_at =
          SaturatingAddTime(req.actual_send_at, req.interval);
      st.next_local_send_at =
          SaturatingSubDuration(st.contract_deadline_at, guard);
      view.wire_next_connect = req.interval;
    }
    st.has_schedule = true;
  } else {
    st.attempt_index += 1;
    st.current_attempt_timed_out = false;
    st.awaiting_relink_retry = false;
    st.current_guard = guard;
    st.configured_interval = req.interval;
    st.base_rx_window = req.base_rx_window;
    if (req.actual_send_at >= st.contract_deadline_at) {
      st.contract_deadline_at = AdvanceContractDeadlinePast(
          st.contract_deadline_at, req.actual_send_at, req.interval);
      st.next_local_send_at =
          SaturatingSubDuration(st.contract_deadline_at, guard);
    }
    view.wire_next_connect =
        SaturatingSubTime(st.contract_deadline_at, req.actual_send_at);
  }

  EarlyRxWindowInput rx_in{};
  rx_in.has_planned_send = req.has_planned_send;
  rx_in.planned_send_at = req.planned_send_at;
  rx_in.actual_send_at = req.actual_send_at;
  rx_in.base_rx_window = req.base_rx_window;
  rx_in.has_required_rx_until = st.required_rx_until != TimePoint{};
  rx_in.required_rx_until = st.required_rx_until;
  view.rx = ComputeEarlyRxWindow(rx_in);
  st.required_rx_until = view.rx.required_rx_until;

  if (req.interval.count() == 0 && new_cycle) {
    view.wire_next_connect_ms = 0;
  } else {
    view.wire_next_connect_ms =
        FloorDurationToPositiveInt64Ms(view.wire_next_connect);
  }

  view.cycle_id = st.cycle_id;
  view.attempt_index = st.attempt_index;
  view.cycle_anchor = st.cycle_anchor;
  view.contract_deadline = st.contract_deadline_at;
  view.next_local_send = st.next_local_send_at;
  return view;
}

inline void ConfirmLogicalPingCycle(LogicalPingCycleState& st) noexcept {
  st.confirmed = true;
  st.active = false;
  st.current_attempt_timed_out = false;
  st.awaiting_relink_retry = false;
}

inline void MarkLogicalPingAttemptTimedOut(LogicalPingCycleState& st) noexcept {
  st.current_attempt_timed_out = true;
}

}  // namespace ae

#endif  // AETHER_CLOUD_CONNECTIONS_PING_SCHEDULE_GUARD_H_
