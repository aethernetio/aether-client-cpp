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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

#include <unity.h>

#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/local_presence_machine.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/types/statistic_counter.h"

namespace ae::test_local_presence {

using Ms = std::chrono::milliseconds;

TimePoint Tp(std::int64_t ms) { return TimePoint{Ms{ms}}; }

Duration Dur(std::int64_t ms) {
  return std::chrono::duration_cast<Duration>(Ms{ms});
}

std::int64_t ToMs(TimePoint tp) {
  return std::chrono::duration_cast<Ms>(tp.time_since_epoch()).count();
}

std::int64_t ToMs(Duration d) {
  return std::chrono::duration_cast<Ms>(d).count();
}

void test_PrefixFormula() {
  auto const R = Dur(100);
  auto const G = kLocalPresenceGuard;
  auto const O = Tp(1050);
  auto const p1 = ComputePrefix1Time(O, R, G);
  auto const p2 = ComputePrefix2Time(O, R, G);
  TEST_ASSERT_EQUAL(870, ToMs(p1));
  TEST_ASSERT_EQUAL(970, ToMs(p2));
  TEST_ASSERT_EQUAL(100, std::chrono::duration_cast<Ms>(p2 - p1).count());
  TEST_ASSERT_EQUAL(30, ToMs(G));
}

void test_ConfirmOnlyAfterPong() {
  ClientConnectivityPolicy policy;
  ServerId const sid{7};
  policy.ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), 99);
  auto* state = policy.FindServerPresence(sid);
  TEST_ASSERT_NOT_NULL(state);
  TEST_ASSERT_FALSE(state->has_confirmed_schedule);
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(sid, Tp(0)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(0)));

  policy.ConfirmServerPong(sid, Tp(1000), Tp(1100), Dur(1000), Dur(300),
                           Dur(100));
  state = policy.FindServerPresence(sid);
  TEST_ASSERT_TRUE(state->has_confirmed_schedule);
  TEST_ASSERT_EQUAL(2050, ToMs(state->confirmed_window_open_local));
  TEST_ASSERT_EQUAL(2350, ToMs(state->confirmed_window_close_local));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, Tp(2050)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, Tp(2350)));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(sid, Tp(2351)));
}

void test_SelectedRttProjectionIgnoresMeasuredPong() {
  auto const selected = Dur(100);
  auto fast = MakeConfirmedSchedule(Tp(1000), Tp(1020), Dur(1000), Dur(1000),
                                    selected);
  auto slow = MakeConfirmedSchedule(Tp(1000), Tp(1400), Dur(1000), Dur(1000),
                                    selected);
  TEST_ASSERT_EQUAL(ToMs(fast.window_open_local), ToMs(slow.window_open_local));
  TEST_ASSERT_EQUAL(ToMs(fast.window_close_local),
                    ToMs(slow.window_close_local));
  TEST_ASSERT_EQUAL(2050, ToMs(fast.window_open_local));
  TEST_ASSERT_TRUE(ToMs(fast.measured_rtt) != ToMs(slow.measured_rtt));
}

void test_PerServerIndependence() {
  ClientConnectivityPolicy policy;
  ServerId const a{1};
  ServerId const b{2};
  policy.ConfigureServerRxTiming(
      a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), 99);
  policy.ConfigureServerRxTiming(
      b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)), 95);
  policy.ConfirmServerPong(a, Tp(0), Tp(100), Dur(1000), Dur(300), Dur(100));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(b, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(a, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
}

void test_OfflineOnlyAfterWindowClose() {
  ClientConnectivityPolicy policy;
  ServerId const sid{3};
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1220)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(1221)));
}

void test_RuntimeIntervalChangeKeepsOldConfirmed() {
  ClientConnectivityPolicy policy;
  ServerId const sid{4};
  policy.ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)));
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  auto const close_before =
      policy.FindServerPresence(sid)->confirmed_window_close_local;

  policy.ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(10000)).WithWindow(Dur(200)));
  auto* state = policy.FindServerPresence(sid);
  TEST_ASSERT_TRUE(state->config_change_pending);
  TEST_ASSERT_EQUAL(10000, ToMs(state->desired.interval));
  TEST_ASSERT_TRUE(state->confirmed_window_close_local == close_before);
  TEST_ASSERT_EQUAL(1000, ToMs(state->confirmed_interval));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(close_before));

  policy.ConfirmServerPong(sid, Tp(500), Tp(560), Dur(10000), Dur(200),
                           Dur(60));
  state = policy.FindServerPresence(sid);
  TEST_ASSERT_FALSE(state->config_change_pending);
  TEST_ASSERT_EQUAL(10000, ToMs(state->confirmed_interval));
}

void test_RuntimePercentile() {
  StatisticsCounter<int, 20> stats;
  for (int i = 1; i <= 20; ++i) {
    stats.Add(i * 10);
  }
  auto const p95 = stats.PercentileValue(95);
  auto const p99 = stats.PercentileValue(99);
  TEST_ASSERT_TRUE(p99 >= p95);
  TEST_ASSERT_EQUAL(stats.percentile<95>(), p95);
  TEST_ASSERT_EQUAL(stats.percentile<99>(), p99);
}

void test_ReliabilityP95VsP99PrefixTimes() {
  StatisticsCounter<Duration, 20> stats;
  for (int i = 1; i <= 20; ++i) {
    stats.Add(Dur(i * 10));
  }
  auto const p95 = stats.PercentileValue(95);
  auto const p99 = stats.PercentileValue(99);
  TEST_ASSERT_TRUE(p99 >= p95);
  auto const O = Tp(5000);
  auto const p1_95 = ComputePrefix1Time(O, p95);
  auto const p1_99 = ComputePrefix1Time(O, p99);
  auto const p2_95 = ComputePrefix2Time(O, p95);
  auto const p2_99 = ComputePrefix2Time(O, p99);
  TEST_ASSERT_TRUE(ToMs(p1_99) <= ToMs(p1_95));
  TEST_ASSERT_TRUE(ToMs(p2_99) <= ToMs(p2_95));
  if (p99 > p95) {
    TEST_ASSERT_TRUE(ToMs(p1_99) < ToMs(p1_95));
    TEST_ASSERT_TRUE(ToMs(p2_99) < ToMs(p2_95));
  }
}

void test_AggregateIgnoresDeselected() {
  ClientConnectivityPolicy policy;
  ServerId const a{10};
  ServerId const b{11};
  policy.ConfirmServerPong(a, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  policy.ConfirmServerPong(b, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
  policy.SetServerSelectedForAggregate(a, false);
  policy.SetServerSelectedForAggregate(b, false);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(50)));
  policy.SetServerSelectedForAggregate(b, true);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
}

void test_OneWayProjection() { TEST_ASSERT_EQUAL(50, ToMs(OneWayFromRtt(Dur(100)))); }

void test_MakeConfirmedScheduleDeterministic() {
  auto s =
      MakeConfirmedSchedule(Tp(1000), Tp(1200), Dur(500), Dur(100), Dur(200));
  TEST_ASSERT_EQUAL(1600, ToMs(s.window_open_local));
  TEST_ASSERT_EQUAL(1700, ToMs(s.window_close_local));
}

void test_ConfigScopeOverrideAndPriority() {
  ClientConnectivityPolicy policy;
  ServerId const a{1};
  ServerId const b{2};
  ServerId const c{3};
  policy.BindServerPriority(a, 0);
  policy.BindServerPriority(b, 1);
  policy.ConfigureServerRxTiming(
      a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)), 99);
  TEST_ASSERT_EQUAL(1000, ToMs(policy.FindServerPresence(a)->desired.interval));
  TEST_ASSERT_EQUAL(AE_PING_INTERVAL_MS,
                    ToMs(policy.FindServerPresence(b)->desired.interval));

  policy.ConfigureRxTimings().ForAllPriorities(
      RxTimingConf::Every(Dur(3000)).WithWindow(Dur(3000)));
  TEST_ASSERT_EQUAL(1000, ToMs(policy.FindServerPresence(a)->desired.interval));
  TEST_ASSERT_EQUAL(3000, ToMs(policy.FindServerPresence(b)->desired.interval));

  policy.ConfigureRxTimings().ForPriority<0>(
      RxTimingConf::Every(Dur(4000)).WithWindow(Dur(4000)));
  TEST_ASSERT_EQUAL(1000, ToMs(policy.FindServerPresence(a)->desired.interval));
  TEST_ASSERT_EQUAL(3000, ToMs(policy.FindServerPresence(b)->desired.interval));

  policy.BindServerPriority(c, 0);
  TEST_ASSERT_EQUAL(4000, ToMs(policy.FindServerPresence(c)->desired.interval));
  TEST_ASSERT_EQUAL(3000, ToMs(policy.FindServerPresence(b)->desired.interval));
}

struct PollStats {
  int status_poll_count{};
  int online_samples{};
  int false_offline_samples{};
  int false_offline_transitions{};
  bool prev_online{false};
  bool have_prev{false};
};

struct PendingPong {
  LocalPresenceMachine::SendSpec spec{};
  TimePoint send_time{};
  TimePoint deliver_at{};
  bool drop{false};
  bool hard_fail{false};
};

class PresenceHarness {
 public:
  using DelayFn = std::function<Duration(PingAttemptKind, int)>;

  explicit PresenceHarness(TimePoint start) : now_{start} {}

  ClientConnectivityPolicy& policy() { return policy_; }
  TimePoint now() const { return now_; }
  PollStats const& poll_stats() const { return poll_stats_; }

  LocalPresenceMachine& machine(ServerId id) { return servers_.at(id).machine; }

  LocalPresenceMachine::Counters const& counters(ServerId id) {
    return servers_.at(id).machine.counters();
  }

  void AddServer(ServerId id, RxTimingConf conf, Duration seed_rtt,
                 std::uint8_t percentile = kDefaultRttReliabilityPercentile,
                 std::size_t priority = 0) {
    policy_.BindServerPriority(id, priority);
    policy_.ConfigureServerRxTiming(id, conf, percentile);
    Server s{};
    s.id = id;
    s.seed_rtt = seed_rtt;
    s.percentile = percentile;
    for (int i = 0; i < 20; ++i) {
      s.stats.Add(seed_rtt);
    }
    s.machine.SetDesired(now_, conf, percentile);
    s.machine.ArmInitial(now_);
    servers_.emplace(id, std::move(s));
  }

  void SetFixedDelay(ServerId id, Duration delay) {
    servers_.at(id).fixed_delay = delay;
  }

  void SetDelayFn(ServerId id, DelayFn fn) { servers_.at(id).delay_fn = std::move(fn); }

  void SetDropKind(ServerId id, PingAttemptKind kind, bool drop) {
    servers_.at(id).drop_kind[static_cast<int>(kind)] = drop;
  }

  void SetConnectivity(ServerId id, bool ok) {
    servers_.at(id).connectivity_ok = ok;
  }

  bool IsLocallyOnline() const { return policy_.IsLocallyOnline(now_); }

  void SyncBlockers() {
    bool need_current = false;
    bool need_request = false;
    for (auto& [id, s] : servers_) {
      static_cast<void>(id);
      need_current = need_current || s.machine.current_window_blocker_held();
      need_request = need_request || s.machine.request_blocker_held();
    }
    if (need_current) {
      if (!have_current_) {
        current_block_ = policy_.AcquireSuspendBlock();
        have_current_ = true;
      }
    } else {
      current_block_.Reset();
      have_current_ = false;
    }
    if (need_request) {
      if (!have_request_) {
        request_block_ = policy_.AcquireSuspendBlock();
        have_request_ = true;
      }
    } else {
      request_block_.Reset();
      have_request_ = false;
    }
  }

  Duration SelectedRtt(ServerId id) {
    auto& s = servers_.at(id);
    if (s.stats.empty()) {
      return s.seed_rtt;
    }
    return s.stats.PercentileValue(s.percentile);
  }

  void Poll(bool expected_connected) {
    auto const online = IsLocallyOnline();
    ++poll_stats_.status_poll_count;
    if (online) {
      ++poll_stats_.online_samples;
    }
    if (expected_connected && !online) {
      ++poll_stats_.false_offline_samples;
      if (poll_stats_.have_prev && poll_stats_.prev_online) {
        ++poll_stats_.false_offline_transitions;
      }
    }
    poll_stats_.prev_online = online;
    poll_stats_.have_prev = true;
  }

  void Process() {
    bool progress = true;
    while (progress) {
      progress = false;
      for (auto& [id, s] : servers_) {
        static_cast<void>(id);
        if (DeliverDue(s)) {
          progress = true;
        }
      }
      for (auto& [id, s] : servers_) {
        static_cast<void>(id);
        if (TickServer(s)) {
          progress = true;
        }
      }
    }
    SyncBlockers();
  }

  void AdvanceTo(TimePoint t) {
    if (t < now_) {
      return;
    }
    while (now_ < t) {
      Process();
      auto const next = NextEventTime();
      if (next == TimePoint::max() || next > t) {
        now_ = t;
        Process();
        return;
      }
      if (next > now_) {
        now_ = next;
      } else {
        now_ = now_ + Dur(1);
      }
    }
    Process();
  }

  void AdvancePolling(Duration total, Duration step, bool expected_connected) {
    auto const end = now_ + total;
    Process();
    while (now_ < end) {
      auto const next_poll = now_ + step;
      for (;;) {
        auto const ev = NextEventTime();
        if (ev == TimePoint::max() || ev > next_poll) {
          break;
        }
        if (ev > now_) {
          now_ = ev;
        }
        Process();
        if (NextEventTime() <= now_) {
          break;
        }
      }
      now_ = next_poll;
      Process();
      Poll(expected_connected);
    }
  }

 private:
  struct Server {
    ServerId id{};
    LocalPresenceMachine machine{};
    StatisticsCounter<Duration, 100> stats{};
    Duration seed_rtt{Dur(100)};
    std::uint8_t percentile{kDefaultRttReliabilityPercentile};
    Duration fixed_delay{Dur(20)};
    DelayFn delay_fn{};
    bool drop_kind[5]{};
    bool connectivity_ok{true};
    int send_count{};
    std::vector<PendingPong> pending{};
  };

  TimePoint NextEventTime() const {
    auto next = TimePoint::max();
    for (auto const& [id, s] : servers_) {
      static_cast<void>(id);
      next = std::min(next, s.machine.PeekNextWake());
      for (auto const& p : s.pending) {
        next = std::min(next, p.deliver_at);
      }
    }
    return next;
  }

  bool DeliverDue(Server& s) {
    bool any = false;
    for (auto it = s.pending.begin(); it != s.pending.end();) {
      if (it->drop) {
        ++it;
        continue;
      }
      if (now_ < it->deliver_at) {
        ++it;
        continue;
      }
      if (it->hard_fail) {
        s.machine.OnHardFailure(it->spec.attempt_id, now_, SelectedRtt(s.id),
                                PresenceRestreamReason::kHardWriteFailure);
      } else {
        auto const measured =
            std::chrono::duration_cast<Duration>(now_ - it->send_time);
        s.stats.Add(measured);
        auto const selected = SelectedRtt(s.id);
        auto outcome = s.machine.OnPong(
            it->spec.attempt_id, it->spec.cycle_id, it->send_time, now_,
            it->spec.wire_interval, it->spec.desired_interval,
            it->spec.rx_window, it->spec.following_open_target, selected);
        if (outcome.disposition ==
            LocalPresenceMachine::PongDisposition::kConfirmedSchedule) {
          policy_.ConfirmServerPong(
              s.id, outcome.schedule.ping_send_time,
              outcome.schedule.pong_receive_time, outcome.schedule.interval,
              outcome.schedule.rx_window, outcome.schedule.selected_rtt);
          auto& st = policy_.EnsureServerPresence(s.id);
          st.confirmed_interval = s.machine.confirmed_interval();
          st.config_change_pending = s.machine.config_change_pending();
        }
      }
      it = s.pending.erase(it);
      any = true;
    }
    return any;
  }

  bool TickServer(Server& s) {
    auto tick = s.machine.TickNow(now_, SelectedRtt(s.id));
    if (!tick.want_send) {
      return false;
    }
    s.machine.OnSendStarting();
    s.machine.OnAttemptSent(tick.send, now_);
    ++s.send_count;
    PendingPong p{};
    p.spec = tick.send;
    p.send_time = now_;
    auto delay = s.fixed_delay;
    if (s.delay_fn) {
      delay = s.delay_fn(tick.send.kind, s.send_count);
    }
    auto const drop =
        !s.connectivity_ok || (ToMs(delay) < 0) || (ToMs(delay) > 5000);
    if (!drop) {
      p.deliver_at = now_ + delay;
      s.pending.push_back(p);
    }
    return true;
  }

  ClientConnectivityPolicy policy_{};
  TimePoint now_{};
  std::map<ServerId, Server> servers_{};
  PollStats poll_stats_{};
  ClientConnectivityPolicy::SuspendBlocker current_block_{};
  ClientConnectivityPolicy::SuspendBlocker request_block_{};
  bool have_current_{false};
  bool have_request_{false};
};

void test_SendWithoutPongDoesNotConfirm() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
               Dur(100));
  rt.SetConnectivity(sid, false);
  rt.AdvanceTo(Tp(0));
  TEST_ASSERT_EQUAL(1, rt.counters(sid).initial);
  TEST_ASSERT_FALSE(rt.IsLocallyOnline());
  TEST_ASSERT_FALSE(rt.machine(sid).has_confirmed_schedule());
}

void test_LongSleepSuspendBetweenPongAndPrefix1() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  auto const interval = Dur(10 * 60 * 1000);
  rt.AddServer(sid, RxTimingConf::Every(interval).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  rt.SyncBlockers();
  TEST_ASSERT_TRUE(rt.policy().GetStatus().can_suspend);
  TEST_ASSERT_TRUE(rt.machine(sid).CanSuspend());

  auto const open = rt.machine(sid).confirmed_window_open();
  auto const prefix1 = ComputePrefix1Time(open, Dur(100));
  rt.AdvanceTo(prefix1 - Dur(1));
  TEST_ASSERT_TRUE(rt.machine(sid).CanSuspend());
  TEST_ASSERT_TRUE(rt.policy().GetStatus().can_suspend);

  rt.AdvanceTo(prefix1);
  TEST_ASSERT_FALSE(rt.machine(sid).CanSuspend());
  TEST_ASSERT_FALSE(rt.policy().GetStatus().can_suspend);

  auto const current_c = rt.machine(sid).current_promised_close();
  TEST_ASSERT_TRUE(current_c == rt.machine(sid).confirmed_window_close() ||
                   ToMs(current_c) <= ToMs(rt.machine(sid).confirmed_window_close()));
  rt.AdvanceTo(current_c);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  rt.AdvanceTo(current_c + Dur(1));
  TEST_ASSERT_FALSE(rt.machine(sid).current_window_blocker_held());
}

void test_CurrentVsNextWindowBlocker() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(10000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  auto const c0 = rt.machine(sid).confirmed_window_close();
  TEST_ASSERT_TRUE(rt.machine(sid).CanSuspend());
  auto const prefix1 =
      ComputePrefix1Time(rt.machine(sid).confirmed_window_open(), Dur(100));
  rt.AdvanceTo(prefix1);
  TEST_ASSERT_TRUE(rt.machine(sid).current_window_blocker_held());
  TEST_ASSERT_EQUAL(ToMs(c0), ToMs(rt.machine(sid).current_promised_close()));
  rt.AdvanceTo(prefix1 + Dur(20));
  auto const c1 = rt.machine(sid).confirmed_window_close();
  TEST_ASSERT_TRUE(ToMs(c1) > ToMs(c0));
  TEST_ASSERT_EQUAL(ToMs(c0), ToMs(rt.machine(sid).current_promised_close()));
}

void test_Prefix1LatePongAfterPrefix2() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  auto const open = rt.machine(sid).confirmed_window_open();
  auto const target_before = rt.machine(sid).last_following_target();
  static_cast<void>(target_before);
  rt.SetDelayFn(sid, [](PingAttemptKind kind, int) {
    if (kind == PingAttemptKind::kPrefix1) {
      return Dur(250);
    }
    return Dur(20);
  });
  auto const prefix1 = ComputePrefix1Time(open, Dur(100));
  rt.AdvanceTo(prefix1 + Dur(260));
  TEST_ASSERT_TRUE(rt.counters(sid).prefix2 >= 1);
  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_samples);
  TEST_ASSERT_TRUE(rt.machine(sid).outstanding_attempt_count() == 0);
  TEST_ASSERT_TRUE(rt.counters(sid).late_pongs >= 1);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_RttTailEntersStatistics() {
  StatisticsCounter<Duration, 100> stats;
  for (int i = 0; i < 100; ++i) {
    stats.Add(Dur(100));
  }
  TEST_ASSERT_EQUAL(100, ToMs(stats.PercentileValue(99)));
  stats.Add(Dur(300));
  TEST_ASSERT_TRUE(ToMs(stats.PercentileValue(99)) >= 300);

  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  rt.SetDelayFn(sid, [](PingAttemptKind kind, int) {
    if (kind == PingAttemptKind::kPrefix1) {
      return Dur(300);
    }
    return Dur(20);
  });
  auto const prefix1 =
      ComputePrefix1Time(rt.machine(sid).confirmed_window_open(), Dur(100));
  rt.AdvanceTo(prefix1 + Dur(310));
  TEST_ASSERT_TRUE(ToMs(rt.SelectedRtt(sid)) >= 300);
}

void test_PxxMissDoesNotRestream() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  TEST_ASSERT_EQUAL(0, rt.counters(sid).restreams);
  rt.SetDelayFn(sid, [](PingAttemptKind kind, int) {
    if (kind == PingAttemptKind::kPrefix1) {
      return Dur(250);
    }
    return Dur(20);
  });
  auto const prefix1 =
      ComputePrefix1Time(rt.machine(sid).confirmed_window_open(), Dur(100));
  rt.AdvanceTo(prefix1 + Dur(110));
  TEST_ASSERT_TRUE(rt.counters(sid).prefix2 >= 1);
  TEST_ASSERT_EQUAL(0, rt.counters(sid).restreams);
  TEST_ASSERT_TRUE(rt.machine(sid).outstanding_attempt_count() >= 1);
}

void test_EnsureLinkedErrorReleasesBlocker() {
  LocalPresenceMachine machine;
  machine.ArmInitial(Tp(0));
  auto tick = machine.TickNow(Tp(0), Dur(100));
  TEST_ASSERT_TRUE(tick.want_send);
  machine.OnSendStarting();
  TEST_ASSERT_TRUE(machine.request_blocker_held());
  machine.OnStartFailed(Tp(0), Dur(100),
                        PresenceRestreamReason::kConnectionUnavailable);
  TEST_ASSERT_FALSE(machine.request_blocker_held());
  TEST_ASSERT_TRUE(machine.CanSuspend());
  TEST_ASSERT_EQUAL(1, machine.counters().restreams);
}

void test_QuarantineKeepsConfirmedUntilClose() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  auto const close = rt.machine(sid).confirmed_window_close();
  TEST_ASSERT_TRUE(ToMs(close) >= 2000);
  rt.machine(sid).OnQuarantine(Tp(1000));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(sid, Tp(1500)));
  TEST_ASSERT_TRUE(rt.policy().IsLocallyOnline(Tp(1500)));
  TEST_ASSERT_TRUE(rt.policy().IsLocallyOnline(close));
  TEST_ASSERT_FALSE(rt.policy().IsLocallyOnline(close + Dur(1)));
}

void test_HardRemovalDropsAggregateImmediately() {
  ClientConnectivityPolicy policy;
  ServerId const a{1};
  ServerId const b{2};
  policy.ConfirmServerPong(a, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  policy.ConfirmServerPong(b, Tp(0), Tp(40), Dur(1000), Dur(200), Dur(40));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
  policy.RemoveServerFromCloud(a);
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(a, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(b, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
  policy.RemoveServerFromCloud(b);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(50)));
}

void test_RuntimeConfigChangeKeepsOldUntilPong() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  auto const close_old = rt.machine(sid).confirmed_window_close();
  rt.policy().ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(10000)).WithWindow(Dur(200)));
  rt.machine(sid).SetDesired(
      rt.now(), RxTimingConf::Every(Dur(10000)).WithWindow(Dur(200)), 99);
  TEST_ASSERT_EQUAL(1000, ToMs(rt.machine(sid).confirmed_interval()));
  TEST_ASSERT_TRUE(rt.machine(sid).confirmed_window_close() == close_old);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_Prefix1SuccessNoPrefix2() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  auto const prefix1 =
      ComputePrefix1Time(rt.machine(sid).confirmed_window_open(), Dur(100));
  rt.AdvanceTo(prefix1 + Dur(20));
  TEST_ASSERT_EQUAL(1, rt.counters(sid).prefix1);
  TEST_ASSERT_EQUAL(0, rt.counters(sid).prefix2);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_MultiServerIndependentSchedules() {
  PresenceHarness rt{Tp(0)};
  ServerId const a{1};
  ServerId const b{2};
  rt.AddServer(a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100),
               99, 0);
  rt.AddServer(b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)), Dur(200),
               95, 1);
  rt.SetFixedDelay(a, Dur(20));
  rt.SetFixedDelay(b, Dur(20));
  rt.AdvanceTo(Tp(40));
  TEST_ASSERT_EQUAL(1000, ToMs(rt.machine(a).confirmed_interval()));
  TEST_ASSERT_EQUAL(3000, ToMs(rt.machine(b).confirmed_interval()));
  rt.SetConnectivity(a, false);
  rt.AdvanceTo(rt.machine(a).confirmed_window_close() + Dur(1));
  TEST_ASSERT_FALSE(rt.policy().IsServerLocallyOnline(a, rt.now()));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(b, rt.now()));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

struct RuntimeReport {
  int confirmed_cycles{};
  Duration duration{};
};

RuntimeReport g_stat_report{};

void test_StatisticalRuntimePollingIsLocallyOnline() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  auto const interval = Dur(1000);
  auto const window = Dur(1000);
  auto const rtt = Dur(100);
  rt.AddServer(sid, RxTimingConf::Every(interval).WithWindow(window), rtt, 99);
  rt.SetDelayFn(sid, [&rt, sid](PingAttemptKind kind, int) {
    auto const prefix1 = rt.counters(sid).prefix1;
    if (kind == PingAttemptKind::kPrefix1) {
      if ((prefix1 % 19) == 0) {
        return Dur(100000);
      }
      if ((prefix1 % 11) == 0) {
        return Dur(150);
      }
      if ((prefix1 % 17) == 0) {
        return Dur(250);
      }
    }
    if ((kind == PingAttemptKind::kPrefix2) && ((prefix1 % 19) == 0)) {
      return Dur(100000);
    }
    return Dur(20);
  });

  rt.AdvanceTo(Tp(20));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto const measure_start = rt.now();
  while (true) {
    rt.AdvancePolling(Dur(10), Dur(10), true);
    auto const elapsed_ms =
        std::chrono::duration_cast<Ms>(rt.now() - measure_start).count();
    if (elapsed_ms >= 300000) {
      break;
    }
    TEST_ASSERT_TRUE(elapsed_ms < 400000);
  }

  g_stat_report.confirmed_cycles = rt.counters(sid).confirmed_pongs;
  g_stat_report.duration =
      std::chrono::duration_cast<Duration>(rt.now() - measure_start);
  auto const cycles = rt.counters(sid).confirmed_pongs;

  std::printf(
      "STATISTICAL runtime\n"
      "  duration_ms=%lld confirmed_pongs=%d status_polls=%d online_samples=%d\n"
      "  false_offline_samples=%d false_offline_transitions=%d\n"
      "  prefix1=%d prefix2=%d post_prefix_retry=%d late_pongs=%d "
      "timeouts=%d recoveries=%d restreams=%d\n"
      "  rtt_percentile=99 selected_rtt_ms=%lld guard_ms=30\n",
      static_cast<long long>(ToMs(g_stat_report.duration)), cycles,
      rt.poll_stats().status_poll_count, rt.poll_stats().online_samples,
      rt.poll_stats().false_offline_samples,
      rt.poll_stats().false_offline_transitions, rt.counters(sid).prefix1,
      rt.counters(sid).prefix2, rt.counters(sid).retry,
      rt.counters(sid).late_pongs, rt.counters(sid).scheduler_timeouts,
      rt.counters(sid).recoveries_to_online, rt.counters(sid).restreams,
      static_cast<long long>(ToMs(rt.SelectedRtt(sid))));

  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_samples);
  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_transitions);
  TEST_ASSERT_TRUE(cycles >= 280);
  TEST_ASSERT_TRUE(cycles <= 330);
  TEST_ASSERT_TRUE(rt.counters(sid).prefix2 > 0);
  TEST_ASSERT_TRUE(rt.counters(sid).retry > 0);
  TEST_ASSERT_EQUAL(0, rt.counters(sid).restreams);
}

void test_FaultOfflineNotBeforeWindowCloseThenRecovery() {
  PresenceHarness rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
               Dur(100));
  rt.SetFixedDelay(sid, Dur(20));
  rt.AdvanceTo(Tp(20));
  rt.AdvancePolling(Dur(2000), Dur(10), true);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto const close = rt.machine(sid).confirmed_window_close();
  rt.SetConnectivity(sid, false);

  auto detected = TimePoint{};
  while (rt.now() < close + Dur(2000)) {
    rt.AdvancePolling(Dur(10), Dur(10), false);
    if (!rt.IsLocallyOnline()) {
      detected = rt.now();
      break;
    }
  }
  TEST_ASSERT_TRUE(detected > close);
  rt.SetConnectivity(sid, true);
  auto const recover_from = rt.now();
  while (rt.now() < recover_from + Dur(2000)) {
    rt.AdvancePolling(Dur(10), Dur(10), false);
    if (rt.IsLocallyOnline()) {
      break;
    }
  }
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

}  // namespace ae::test_local_presence

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_local_presence::test_PrefixFormula);
  RUN_TEST(ae::test_local_presence::test_ConfirmOnlyAfterPong);
  RUN_TEST(ae::test_local_presence::test_SelectedRttProjectionIgnoresMeasuredPong);
  RUN_TEST(ae::test_local_presence::test_PerServerIndependence);
  RUN_TEST(ae::test_local_presence::test_OfflineOnlyAfterWindowClose);
  RUN_TEST(ae::test_local_presence::test_RuntimeIntervalChangeKeepsOldConfirmed);
  RUN_TEST(ae::test_local_presence::test_RuntimePercentile);
  RUN_TEST(ae::test_local_presence::test_ReliabilityP95VsP99PrefixTimes);
  RUN_TEST(ae::test_local_presence::test_AggregateIgnoresDeselected);
  RUN_TEST(ae::test_local_presence::test_OneWayProjection);
  RUN_TEST(ae::test_local_presence::test_MakeConfirmedScheduleDeterministic);
  RUN_TEST(ae::test_local_presence::test_ConfigScopeOverrideAndPriority);
  RUN_TEST(ae::test_local_presence::test_SendWithoutPongDoesNotConfirm);
  RUN_TEST(ae::test_local_presence::test_LongSleepSuspendBetweenPongAndPrefix1);
  RUN_TEST(ae::test_local_presence::test_CurrentVsNextWindowBlocker);
  RUN_TEST(ae::test_local_presence::test_Prefix1LatePongAfterPrefix2);
  RUN_TEST(ae::test_local_presence::test_RttTailEntersStatistics);
  RUN_TEST(ae::test_local_presence::test_PxxMissDoesNotRestream);
  RUN_TEST(ae::test_local_presence::test_EnsureLinkedErrorReleasesBlocker);
  RUN_TEST(ae::test_local_presence::test_QuarantineKeepsConfirmedUntilClose);
  RUN_TEST(ae::test_local_presence::test_HardRemovalDropsAggregateImmediately);
  RUN_TEST(ae::test_local_presence::test_RuntimeConfigChangeKeepsOldUntilPong);
  RUN_TEST(ae::test_local_presence::test_Prefix1SuccessNoPrefix2);
  RUN_TEST(ae::test_local_presence::test_MultiServerIndependentSchedules);
  RUN_TEST(ae::test_local_presence::test_StatisticalRuntimePollingIsLocallyOnline);
  RUN_TEST(ae::test_local_presence::test_FaultOfflineNotBeforeWindowCloseThenRecovery);
  return UNITY_END();
}
