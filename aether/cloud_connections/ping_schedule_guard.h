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
#include "aether/config.h"
#include "aether/receive_schedule.h"

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

#ifdef AE_PING_GUARD_OVERRIDE_US
inline Duration ResolvePingSendGuard(Duration min_rtt, Duration p99_rtt,
                                     Duration ping_interval) noexcept {
  (void)min_rtt;
  (void)p99_rtt;
  auto const fixed =
      Duration{static_cast<Duration::rep>(AE_PING_GUARD_OVERRIDE_US)};
  return ClampPingSendGuard(fixed, ping_interval);
}
#else
inline Duration ResolvePingSendGuard(Duration min_rtt, Duration p99_rtt,
                                     Duration ping_interval) noexcept {
  return ClampPingSendGuard(ComputePingSendGuard(min_rtt, p99_rtt),
                            ping_interval);
}
#endif

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
  return ResolvePingSendGuard(min_rtt, p99_rtt, ping_interval);
}

inline Duration SaturatingSubDurationValue(Duration a, Duration b) noexcept {
  if (b.count() == 0) {
    return a;
  }
  if (a.count() <= b.count()) {
    return Duration{};
  }
  return Duration{
      static_cast<typename Duration::rep>(a.count() - b.count())};
}

inline Duration SaturatingAddDuration(Duration a, Duration b) noexcept;

inline Duration SaturatingMulDuration(Duration d,
                                      std::uint32_t factor) noexcept {
  if (factor == 0 || d.count() <= 0) {
    return Duration{};
  }
  auto const max = Duration::max();
  std::uint64_t acc = static_cast<std::uint64_t>(d.count());
  auto const step = static_cast<std::uint64_t>(d.count());
  auto const max_count = static_cast<std::uint64_t>(max.count());
  for (std::uint32_t i = 1; i < factor; ++i) {
    if (acc > max_count - step) {
      return max;
    }
    acc += step;
  }
  if (acc > max_count) {
    return max;
  }
  return Duration{static_cast<Duration::rep>(acc)};
}

inline Duration DivideDurationFloor(Duration total,
                                    std::uint32_t divisor) noexcept {
  if (divisor == 0 || total.count() <= 0) {
    return Duration{};
  }
  return Duration{static_cast<Duration::rep>(total.count() / divisor)};
}

inline bool CanSchedulePreDeadlineSameCycleRetry(
    TimePoint now, TimePoint contract_deadline, std::uint32_t attempt_index,
    std::uint8_t pre_deadline_retry_count) noexcept {
  if (contract_deadline == TimePoint{} || now >= contract_deadline) {
    return true;
  }
  return attempt_index <= pre_deadline_retry_count;
}

inline void IncrementLogicalPingAttemptIndex(
    std::uint32_t& attempt_index) noexcept {
  if (attempt_index < std::numeric_limits<std::uint32_t>::max()) {
    ++attempt_index;
  }
}

inline constexpr Duration kPingSchedulerMargin =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{10});
// Fixed allowance for local timeout-to-retry dispatch/scheduling latency.
// Network uncertainty is accounted for separately by RTT p99 and the ping
// guard. Sized from first-request-loss p99 characterization (combined
// required-extra p99 ~40ms, observed max ~48ms); raised to 60ms after a
// residual ~3ms TCP outlier at 50ms.
inline constexpr Duration kPingRetryDispatchMargin =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{60});
inline constexpr Duration kPingMinLossTimeout =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{50});
inline constexpr Duration kPingP99TimeoutMargin =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{10});
inline constexpr Duration kPingRttEstimate =
    std::chrono::duration_cast<Duration>(std::chrono::milliseconds{200});

struct PingRetryBudgetInput {
  Duration interval{};
  Duration guard{};
  Duration raw_timeout{};
  Duration p99_rtt{};
  std::uint8_t retry_count{kDefaultPingRetryCount};
};

struct PingRetryBudget {
  Duration retry_one_way_budget{};
  Duration scheduler_margin{};
  Duration retry_dispatch_margin{};
  Duration loss_timeout{};
  Duration retry_reserve{};
  Duration attempt_lead{};
  Duration max_timeout_for_predeadline_retry{};
  bool predeadline_retry_guaranteed{true};
};

// loss_timeout = max(raw, p99+10ms, 50ms), capped so N retries can still
// finish before Tn when the interval allows it.
// retry_reserve = N*loss_timeout + p99/2 + N*scheduler_margin
//                 + N*retry_dispatch_margin
//               ≈ (N + 0.5)*p99 + N*D + N*scheduler (when uncapped)
// attempt_lead = guard + retry_reserve
inline PingRetryBudget ComputePingRetryBudget(
    PingRetryBudgetInput const& in) noexcept {
  PingRetryBudget out{};
  auto const retry_count =
      in.retry_count > kMaxPingRetryCount ? kMaxPingRetryCount : in.retry_count;
  out.scheduler_margin = kPingSchedulerMargin;
  out.retry_dispatch_margin = kPingRetryDispatchMargin;
  out.retry_one_way_budget = in.p99_rtt / 2;

  Duration loss = in.raw_timeout;
  auto const p99_with_margin =
      SaturatingAddDuration(in.p99_rtt, kPingP99TimeoutMargin);
  if (p99_with_margin > loss) {
    loss = p99_with_margin;
  }
  if (kPingMinLossTimeout > loss) {
    loss = kPingMinLossTimeout;
  }

  auto const one_ms =
      std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1});
  auto const scheduler_total =
      SaturatingMulDuration(out.scheduler_margin, retry_count);
  auto const dispatch_total =
      SaturatingMulDuration(out.retry_dispatch_margin, retry_count);
  auto remaining = in.interval;
  auto subtract_ok = [&](Duration d) {
    if (d.count() == 0) {
      return true;
    }
    if (remaining.count() <= d.count()) {
      remaining = Duration{};
      return false;
    }
    remaining = SaturatingSubDurationValue(remaining, d);
    return remaining.count() > 0;
  };
  bool budget_fits = subtract_ok(in.guard) &&
                     subtract_ok(out.retry_one_way_budget);
  if (retry_count > 0) {
    budget_fits = budget_fits && subtract_ok(scheduler_total) &&
                  subtract_ok(dispatch_total);
  }
  budget_fits = budget_fits && subtract_ok(one_ms);
  if (retry_count > 0) {
    out.max_timeout_for_predeadline_retry =
        DivideDurationFloor(remaining, retry_count);
    out.predeadline_retry_guaranteed = budget_fits && remaining.count() > 0;
    if (out.predeadline_retry_guaranteed &&
        loss > out.max_timeout_for_predeadline_retry) {
      loss = out.max_timeout_for_predeadline_retry;
    }
  } else {
    out.max_timeout_for_predeadline_retry = Duration{};
    out.predeadline_retry_guaranteed = budget_fits;
  }
  out.loss_timeout = loss.count() == 0 ? kPingMinLossTimeout : loss;
  auto const loss_total = SaturatingMulDuration(out.loss_timeout, retry_count);
  out.retry_reserve = SaturatingAddDuration(
      SaturatingAddDuration(
          SaturatingAddDuration(loss_total, out.retry_one_way_budget),
          scheduler_total),
      dispatch_total);
  out.attempt_lead = SaturatingAddDuration(in.guard, out.retry_reserve);
  if (in.interval > one_ms) {
    auto const max_lead = SaturatingSubDurationValue(in.interval, one_ms);
    if (out.attempt_lead > max_lead) {
      out.attempt_lead = max_lead;
      out.predeadline_retry_guaranteed = false;
    }
  } else {
    out.attempt_lead = Duration{};
    out.predeadline_retry_guaranteed = false;
  }
  if (out.loss_timeout.count() == 0) {
    out.loss_timeout = kPingMinLossTimeout;
    out.predeadline_retry_guaranteed = false;
  }
  return out;
}

template <typename Stats>
inline PingRetryBudget ComputePingRetryBudgetFromStats(
    Stats const& stats, Duration ping_interval, Duration raw_timeout,
    std::uint8_t retry_count = kDefaultPingRetryCount) noexcept {
  Duration p99_rtt = kPingRttEstimate;
  Duration min_rtt = kPingRttEstimate;
  if (!stats.empty()) {
    min_rtt = stats.min();
    p99_rtt = stats.template percentile<99>();
  }
  auto const guard = ResolvePingSendGuard(min_rtt, p99_rtt, ping_interval);
  Duration raw = raw_timeout;
  if (raw.count() == 0) {
    raw = kPingRttEstimate;
  }
  return ComputePingRetryBudget(PingRetryBudgetInput{
      ping_interval, guard, raw, p99_rtt, retry_count});
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
  bool has_nominal_ping{false};
  TimePoint nominal_ping_at{};
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

// required_end = max(Tn + W, previous required, Ai + W). Negative diffs are 0.
inline EarlyRxWindowOutput ComputeEarlyRxWindow(
    EarlyRxWindowInput const& in) noexcept {
  EarlyRxWindowOutput out{};
  if (in.has_nominal_ping && in.nominal_ping_at > in.actual_send_at) {
    out.early_by = SaturatingSubTime(in.nominal_ping_at, in.actual_send_at);
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
  if (in.has_nominal_ping) {
    raise(SaturatingAddTime(in.nominal_ping_at, in.base_rx_window));
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
                                    std::uint64_t current_cycle_id,
                                    std::uint64_t result_cycle_id,
                                    std::uint32_t current_attempt,
                                    std::uint32_t result_attempt,
                                    bool current_attempt_timed_out,
                                    bool result_is_ok_or_late) noexcept {
  if (!cycle_active || cycle_confirmed) {
    return false;
  }
  if (result_cycle_id != current_cycle_id || result_attempt == 0) {
    return false;
  }
  (void)current_attempt_timed_out;
  if (result_is_ok_or_late) {
    return true;
  }
  return result_attempt == current_attempt;
}

struct LogicalPingCycleState {
  std::uint64_t cycle_id{0};
  bool active{false};
  bool confirmed{false};
  bool has_schedule{false};
  bool bootstrap{false};
  bool current_attempt_timed_out{false};
  bool awaiting_relink_retry{false};
  bool predeadline_retry_guaranteed{true};
  std::uint32_t attempt_index{0};
  TimePoint first_attempt_at{};
  TimePoint actual_attempt_send_at{};
  TimePoint nominal_ping_at{};
  TimePoint next_nominal_ping_at{};
  TimePoint next_local_send_at{};
  TimePoint required_rx_until{};
  Duration configured_interval{};
  Duration base_rx_window{};
  Duration current_guard{};
  Duration current_retry_reserve{};
  Duration current_attempt_lead{};
  Duration current_loss_timeout{};
};

struct LogicalPingAttemptRequest {
  TimePoint actual_send_at{};
  Duration interval{};
  Duration guard{};
  Duration attempt_lead{};
  Duration retry_reserve{};
  Duration loss_timeout{};
  Duration base_rx_window{};
  bool predeadline_retry_guaranteed{true};
  bool announce_unknown{false};
};

struct LogicalPingAttemptView {
  std::uint64_t cycle_id{0};
  std::uint32_t attempt_index{0};
  bool is_retry{false};
  bool started_new_cycle{false};
  bool bootstrap{false};
  TimePoint first_attempt_at{};
  TimePoint actual_attempt_send_at{};
  TimePoint nominal_ping_at{};
  TimePoint next_nominal_ping_at{};
  TimePoint cycle_anchor{};
  TimePoint contract_deadline{};
  TimePoint next_local_send{};
  Duration wire_next_connect{};
  std::int64_t wire_next_connect_ms{0};
  Duration attempt_lead{};
  Duration retry_reserve{};
  Duration loss_timeout{};
  bool predeadline_retry_guaranteed{true};
  EarlyRxWindowOutput rx{};
};

inline Duration ScheduleLeadFor(LogicalPingAttemptRequest const& req,
                                Duration guard) noexcept {
  if (req.attempt_lead.count() > 0) {
    return req.attempt_lead;
  }
  return guard;
}

inline LogicalPingAttemptView ApplyLogicalPingAttempt(
    LogicalPingCycleState& st, LogicalPingAttemptRequest const& req) noexcept {
  LogicalPingAttemptView view{};
  bool const new_cycle = !st.active || st.confirmed;
  view.started_new_cycle = new_cycle;
  view.is_retry = !new_cycle;

  auto const guard = ClampPingSendGuard(req.guard, req.interval);
  auto const lead = ScheduleLeadFor(req, guard);

  st.current_guard = guard;
  st.current_attempt_lead = lead;
  st.current_retry_reserve = req.retry_reserve;
  st.current_loss_timeout = req.loss_timeout;
  st.predeadline_retry_guaranteed = req.predeadline_retry_guaranteed;
  st.configured_interval = req.interval;
  st.base_rx_window = req.base_rx_window;
  st.actual_attempt_send_at = req.actual_send_at;

  if (req.announce_unknown) {
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
      st.first_attempt_at = req.actual_send_at;
      st.nominal_ping_at = req.actual_send_at;
      st.next_nominal_ping_at = req.actual_send_at;
      st.bootstrap = false;
    } else {
      IncrementLogicalPingAttemptIndex(st.attempt_index);
      st.current_attempt_timed_out = false;
      st.awaiting_relink_retry = false;
    }
    view.wire_next_connect = Duration{};
    view.wire_next_connect_ms = 0;
    st.next_local_send_at = TimePoint::max();
  } else if (new_cycle) {
    st.cycle_id += 1;
    if (st.cycle_id == 0) {
      st.cycle_id = 1;
    }
    st.active = true;
    st.confirmed = false;
    st.current_attempt_timed_out = false;
    st.awaiting_relink_retry = false;
    st.attempt_index = 1;

    if (!st.has_schedule) {
      st.bootstrap = true;
      st.nominal_ping_at = req.actual_send_at;
      st.next_nominal_ping_at =
          SaturatingAddTime(st.nominal_ping_at, req.interval);
      st.first_attempt_at = req.actual_send_at;
      view.wire_next_connect = req.interval;
      st.has_schedule = req.interval.count() != 0;
    } else {
      st.bootstrap = false;
      auto tn = st.next_nominal_ping_at;
      auto tn1 = SaturatingAddTime(tn, req.interval);
      if (req.actual_send_at >= tn1) {
        tn1 = AdvanceContractDeadlinePast(tn1, req.actual_send_at,
                                          req.interval);
        tn = SaturatingSubDuration(tn1, req.interval);
      }
      st.nominal_ping_at = tn;
      st.next_nominal_ping_at = tn1;
      st.first_attempt_at = SaturatingSubDuration(tn, lead);
      view.wire_next_connect =
          SaturatingSubTime(st.next_nominal_ping_at, req.actual_send_at);
    }
    st.next_local_send_at =
        SaturatingSubDuration(st.next_nominal_ping_at, lead);
  } else {
    IncrementLogicalPingAttemptIndex(st.attempt_index);
    st.current_attempt_timed_out = false;
    st.awaiting_relink_retry = false;
    if (req.actual_send_at >= st.next_nominal_ping_at) {
      auto const old_next = st.next_nominal_ping_at;
      st.next_nominal_ping_at = AdvanceContractDeadlinePast(
          st.next_nominal_ping_at, req.actual_send_at, req.interval);
      auto const shift =
          SaturatingSubTime(st.next_nominal_ping_at, old_next);
      st.nominal_ping_at = SaturatingAddTime(st.nominal_ping_at, shift);
      st.next_local_send_at =
          SaturatingSubDuration(st.next_nominal_ping_at, lead);
    }
    view.wire_next_connect =
        SaturatingSubTime(st.next_nominal_ping_at, req.actual_send_at);
  }

  EarlyRxWindowInput rx_in{};
  rx_in.has_nominal_ping = st.nominal_ping_at != TimePoint{} ||
                           st.has_schedule || new_cycle;
  rx_in.nominal_ping_at = st.nominal_ping_at;
  rx_in.actual_send_at = req.actual_send_at;
  rx_in.base_rx_window = req.base_rx_window;
  rx_in.has_required_rx_until = st.required_rx_until != TimePoint{};
  rx_in.required_rx_until = st.required_rx_until;
  view.rx = ComputeEarlyRxWindow(rx_in);
  st.required_rx_until = view.rx.required_rx_until;

  if (req.announce_unknown ||
      (req.interval.count() == 0 && new_cycle)) {
    view.wire_next_connect_ms = 0;
  } else {
    view.wire_next_connect_ms =
        FloorDurationToPositiveInt64Ms(view.wire_next_connect);
  }

  view.cycle_id = st.cycle_id;
  view.attempt_index = st.attempt_index;
  view.bootstrap = st.bootstrap;
  view.first_attempt_at = st.first_attempt_at;
  view.actual_attempt_send_at = st.actual_attempt_send_at;
  view.nominal_ping_at = st.nominal_ping_at;
  view.next_nominal_ping_at = st.next_nominal_ping_at;
  view.cycle_anchor = st.nominal_ping_at;
  view.contract_deadline = st.next_nominal_ping_at;
  view.next_local_send = st.next_local_send_at;
  view.attempt_lead = lead;
  view.retry_reserve = req.retry_reserve;
  view.loss_timeout = req.loss_timeout;
  view.predeadline_retry_guaranteed = st.predeadline_retry_guaranteed;
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
