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
#include <map>
#include <vector>

#include <unity.h>

#include "aether/clock.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/types/statistic_counter.h"

namespace ae::test_local_presence {

using Ms = std::chrono::milliseconds;

TimePoint Tp(std::int64_t ms) { return TimePoint{Ms{ms}}; }

Duration Dur(std::int64_t ms) { return std::chrono::duration_cast<Duration>(Ms{ms}); }

std::int64_t ToMs(TimePoint tp) {
  return std::chrono::duration_cast<Ms>(tp.time_since_epoch()).count();
}

std::int64_t ToMs(Duration d) { return std::chrono::duration_cast<Ms>(d).count(); }

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
  policy.ConfigureServerRxTiming(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
                                 99);
  auto* state = policy.FindServerPresence(sid);
  TEST_ASSERT_NOT_NULL(state);
  TEST_ASSERT_FALSE(state->has_confirmed_schedule);
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(sid, Tp(0)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(0)));

  policy.ConfirmServerPong(sid, Tp(1000), Tp(1100), Dur(1000), Dur(300));
  state = policy.FindServerPresence(sid);
  TEST_ASSERT_TRUE(state->has_confirmed_schedule);
  TEST_ASSERT_EQUAL(2050, ToMs(state->confirmed_window_open_local));
  TEST_ASSERT_EQUAL(2350, ToMs(state->confirmed_window_close_local));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, Tp(2050)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, Tp(2350)));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(sid, Tp(2351)));
}

void test_PerServerIndependence() {
  ClientConnectivityPolicy policy;
  ServerId const a{1};
  ServerId const b{2};
  policy.ConfigureServerRxTiming(a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
                                 99);
  policy.ConfigureServerRxTiming(b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)),
                                 95);
  policy.ConfirmServerPong(a, Tp(0), Tp(100), Dur(1000), Dur(300));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(b, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(a, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));

  auto* sa = policy.FindServerPresence(a);
  auto* sb = policy.FindServerPresence(b);
  TEST_ASSERT_EQUAL(1000, ToMs(sa->desired.interval));
  TEST_ASSERT_EQUAL(3000, ToMs(sb->desired.interval));
  TEST_ASSERT_EQUAL(99, sa->rtt_reliability_percentile);
  TEST_ASSERT_EQUAL(95, sb->rtt_reliability_percentile);

  auto const Ra = Dur(100);
  auto const Rb = Dur(200);
  TEST_ASSERT_EQUAL(870, ToMs(ComputePrefix1Time(Tp(1050), Ra)));
  TEST_ASSERT_EQUAL(2720, ToMs(ComputePrefix1Time(Tp(3050), Rb)));
}

void test_OfflineOnlyAfterWindowClose() {
  ClientConnectivityPolicy policy;
  ServerId const sid{3};
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(200));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1220)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(1221)));
}

void test_RuntimeIntervalChangeKeepsOldConfirmed() {
  ClientConnectivityPolicy policy;
  ServerId const sid{4};
  policy.ConfigureServerRxTiming(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)));
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(200));
  auto const close_before = policy.FindServerPresence(sid)->confirmed_window_close_local;

  policy.ConfigureServerRxTiming(sid, RxTimingConf::Every(Dur(10000)).WithWindow(Dur(200)));
  auto* state = policy.FindServerPresence(sid);
  TEST_ASSERT_TRUE(state->config_change_pending);
  TEST_ASSERT_EQUAL(10000, ToMs(state->desired.interval));
  TEST_ASSERT_TRUE(state->confirmed_window_close_local == close_before);
  TEST_ASSERT_EQUAL(1000, ToMs(state->confirmed_interval));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(close_before));

  policy.ConfirmServerPong(sid, Tp(500), Tp(560), Dur(10000), Dur(200));
  state = policy.FindServerPresence(sid);
  TEST_ASSERT_FALSE(state->config_change_pending);
  TEST_ASSERT_EQUAL(10000, ToMs(state->confirmed_interval));

  policy.ConfigureServerRxTiming(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)));
  TEST_ASSERT_EQUAL(10000, ToMs(policy.FindServerPresence(sid)->confirmed_interval));
  policy.ConfirmServerPong(sid, Tp(20000), Tp(20040), Dur(1000), Dur(200));
  TEST_ASSERT_EQUAL(1000, ToMs(policy.FindServerPresence(sid)->confirmed_interval));
}

void test_RecoveryAfterOffline() {
  ClientConnectivityPolicy policy;
  ServerId const sid{5};
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(100));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(100000)));
  policy.ConfirmServerPong(sid, Tp(100100), Tp(100140), Dur(1000), Dur(100));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(100140)));
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
  policy.ConfirmServerPong(a, Tp(0), Tp(40), Dur(1000), Dur(200));
  policy.ConfirmServerPong(b, Tp(0), Tp(40), Dur(1000), Dur(200));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
  policy.SetServerSelectedForAggregate(a, false);
  policy.SetServerSelectedForAggregate(b, false);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(50)));
  policy.SetServerSelectedForAggregate(b, true);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
}

void test_OneWayProjection() {
  TEST_ASSERT_EQUAL(50, ToMs(OneWayFromRtt(Dur(100))));
}

void test_MakeConfirmedScheduleDeterministic() {
  auto s = MakeConfirmedSchedule(Tp(1000), Tp(1200), Dur(500), Dur(100));
  TEST_ASSERT_EQUAL(1600, ToMs(s.window_open_local));
  TEST_ASSERT_EQUAL(1700, ToMs(s.window_close_local));
}

void test_PlanPrefix1FailSchedulesPrefix2() {
  auto const plan = PlanAfterFailedAttempt(true, Tp(1050), Tp(1350),
                                           PingAttemptKind::kPrefix1, Tp(870 + 100),
                                           Dur(100));
  TEST_ASSERT_EQUAL(static_cast<int>(PingAttemptKind::kPrefix2),
                    static_cast<int>(plan.kind));
  TEST_ASSERT_EQUAL(970, ToMs(plan.when));
  TEST_ASSERT_FALSE(plan.mark_offline);
}

void test_PlanPrefix2FailRetriesWhileOnline() {
  auto const plan = PlanAfterFailedAttempt(true, Tp(1050), Tp(1350),
                                           PingAttemptKind::kPrefix2, Tp(1070),
                                           Dur(100));
  TEST_ASSERT_EQUAL(static_cast<int>(PingAttemptKind::kRetry),
                    static_cast<int>(plan.kind));
  TEST_ASSERT_EQUAL(1170, ToMs(plan.when));
  TEST_ASSERT_FALSE(plan.mark_offline);
}

void test_PlanAfterCloseIsRecoveryOffline() {
  auto const plan = PlanAfterFailedAttempt(true, Tp(1050), Tp(1350),
                                           PingAttemptKind::kRetry, Tp(1351),
                                           Dur(100));
  TEST_ASSERT_EQUAL(static_cast<int>(PingAttemptKind::kRecovery),
                    static_cast<int>(plan.kind));
  TEST_ASSERT_TRUE(plan.mark_offline);
}

void test_StaleAttemptRejected() {
  TEST_ASSERT_TRUE(IsCurrentPingAttempt(4, 4));
  TEST_ASSERT_FALSE(IsCurrentPingAttempt(5, 4));
}

void test_PlanSuccessSchedulesPrefix1() {
  auto const plan = PlanAfterSuccessfulPong(Tp(1050), Tp(100), Dur(100), false);
  TEST_ASSERT_EQUAL(static_cast<int>(PingAttemptKind::kPrefix1),
                    static_cast<int>(plan.kind));
  TEST_ASSERT_EQUAL(870, ToMs(plan.when));
}

struct AttemptLog {
  ServerId server{};
  std::uint64_t attempt_id{};
  PingAttemptKind kind{PingAttemptKind::kInitial};
  TimePoint send_time{};
};

struct PollStats {
  int status_poll_count{};
  int online_samples{};
  int false_offline_samples{};
  int false_offline_transitions{};
  Duration max_false_offline_duration{};
  Duration current_false_offline_duration{};
  bool prev_online{false};
  bool have_prev{false};
};

struct SimServer {
  ServerId id{};
  Duration schedule_rtt{Dur(100)};
  Duration pong_delay{Dur(20)};
  bool connectivity_ok{true};
  bool stopped{false};
  bool transport_open{true};
  bool in_flight{false};
  int fail_next_attempts{0};
  std::uint64_t active_attempt_id{0};
  PingAttemptKind next_kind{PingAttemptKind::kInitial};
  TimePoint next_due{};
  TimePoint inflight_send{};
  TimePoint inflight_timeout_at{};
  TimePoint inflight_pong_at{};
  Duration inflight_interval{};
  Duration inflight_window{};
  PingAttemptKind inflight_kind{PingAttemptKind::kInitial};
  std::uint64_t inflight_id{0};
  bool inflight_should_fail{false};
};

struct SimCounters {
  int prefix1{};
  int prefix2{};
  int retry{};
  int recovery{};
  int initial{};
  int timeouts{};
  int confirmed_pongs{};
  int recoveries_to_online{};
};

class LocalPresenceRuntime {
 public:
  explicit LocalPresenceRuntime(TimePoint start) : now_{start} {}

  ClientConnectivityPolicy& policy() { return policy_; }
  TimePoint now() const { return now_; }
  SimCounters const& counters() const { return counters_; }
  PollStats const& poll_stats() const { return poll_stats_; }
  std::vector<AttemptLog> const& attempts() const { return attempts_; }
  SimServer& server(ServerId id) { return servers_.at(id); }

  bool IsLocallyOnline() const { return policy_.IsLocallyOnline(now_); }

  void AddServer(ServerId id, RxTimingConf conf, Duration rtt,
                 std::uint8_t percentile = kDefaultRttReliabilityPercentile) {
    policy_.ConfigureServerRxTiming(id, conf, percentile);
    SimServer s{};
    s.id = id;
    s.schedule_rtt = rtt;
    s.next_due = now_;
    s.next_kind = PingAttemptKind::kInitial;
    servers_.emplace(id, s);
  }

  void Quarantine(ServerId id) {
    auto& s = servers_.at(id);
    s.stopped = true;
    s.in_flight = false;
    s.next_due = TimePoint::max();
    policy_.SetServerSelectedForAggregate(id, false);
    policy_.InvalidateConfirmedSchedule(id);
  }

  void Release(ServerId id) {
    auto& s = servers_.at(id);
    s.stopped = false;
    s.fail_next_attempts = 0;
    s.connectivity_ok = true;
    policy_.SetServerSelectedForAggregate(id, true);
    s.next_due = now_;
    s.next_kind = PingAttemptKind::kInitial;
  }

  void Poll(bool expected_connected) {
    auto const online = IsLocallyOnline();
    ++poll_stats_.status_poll_count;
    if (online) {
      ++poll_stats_.online_samples;
      poll_stats_.current_false_offline_duration = {};
    }
    if (expected_connected && !online) {
      ++poll_stats_.false_offline_samples;
      poll_stats_.current_false_offline_duration =
          poll_stats_.current_false_offline_duration + Dur(10);
      if (poll_stats_.current_false_offline_duration >
          poll_stats_.max_false_offline_duration) {
        poll_stats_.max_false_offline_duration =
            poll_stats_.current_false_offline_duration;
      }
      if (poll_stats_.have_prev && poll_stats_.prev_online) {
        ++poll_stats_.false_offline_transitions;
      }
    }
    poll_stats_.prev_online = online;
    poll_stats_.have_prev = true;
  }

  void ProcessDue() {
    bool progress = true;
    while (progress) {
      progress = false;
      for (auto& [id, s] : servers_) {
        static_cast<void>(id);
        if (s.stopped || !s.in_flight) {
          continue;
        }
        if (!s.inflight_should_fail && now_ >= s.inflight_pong_at) {
          CompleteSuccess(s);
          progress = true;
        }
      }
      for (auto& [id, s] : servers_) {
        static_cast<void>(id);
        if (s.stopped || !s.in_flight) {
          continue;
        }
        if (now_ >= s.inflight_timeout_at) {
          CompleteTimeout(s);
          progress = true;
        }
      }
      for (auto& [id, s] : servers_) {
        static_cast<void>(id);
        if (s.stopped || s.in_flight) {
          continue;
        }
        if (now_ >= s.next_due) {
          StartSend(s);
          progress = true;
        }
      }
    }
  }

  void AdvancePolling(Duration total, Duration step, bool expected_connected) {
    auto const end = now_ + total;
    ProcessDue();
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
        ProcessDue();
        if (NextEventTime() <= now_) {
          break;
        }
      }
      now_ = next_poll;
      ProcessDue();
      Poll(expected_connected);
    }
  }

  void AdvanceTo(TimePoint t) {
    if (t < now_) {
      return;
    }
    for (;;) {
      ProcessDue();
      if (now_ >= t) {
        return;
      }
      auto const next = NextEventTime();
      if (next == TimePoint::max() || next > t) {
        now_ = t;
        ProcessDue();
        return;
      }
      if (next <= now_) {
        now_ = t;
        ProcessDue();
        return;
      }
      now_ = next;
    }
  }

 private:
  TimePoint NextEventTime() const {
    auto next = TimePoint::max();
    for (auto const& [id, s] : servers_) {
      static_cast<void>(id);
      if (s.stopped) {
        continue;
      }
      if (s.in_flight) {
        if (!s.inflight_should_fail) {
          next = std::min(next, s.inflight_pong_at);
        }
        next = std::min(next, s.inflight_timeout_at);
      } else {
        next = std::min(next, s.next_due);
      }
    }
    return next;
  }
  void CountKind(PingAttemptKind kind) {
    switch (kind) {
      case PingAttemptKind::kPrefix1:
        ++counters_.prefix1;
        break;
      case PingAttemptKind::kPrefix2:
        ++counters_.prefix2;
        break;
      case PingAttemptKind::kRetry:
        ++counters_.retry;
        break;
      case PingAttemptKind::kRecovery:
        ++counters_.recovery;
        break;
      case PingAttemptKind::kInitial:
        ++counters_.initial;
        break;
    }
  }

  void StartSend(SimServer& s) {
    auto& presence = policy_.EnsureServerPresence(s.id);
    ++s.active_attempt_id;
    presence.current_attempt_id = s.active_attempt_id;
    presence.current_attempt_kind = s.next_kind;
    s.in_flight = true;
    s.inflight_id = s.active_attempt_id;
    s.inflight_kind = s.next_kind;
    s.inflight_send = now_;
    s.inflight_interval = presence.desired.interval;
    s.inflight_window = presence.desired.rx_window;
    s.inflight_timeout_at = now_ + s.schedule_rtt;
    s.inflight_pong_at = now_ + s.pong_delay;
    s.inflight_should_fail = !s.connectivity_ok || (s.fail_next_attempts > 0);
    if (s.fail_next_attempts > 0) {
      --s.fail_next_attempts;
    }
    CountKind(s.next_kind);
    attempts_.push_back(AttemptLog{s.id, s.inflight_id, s.inflight_kind, now_});
  }

  void CompleteSuccess(SimServer& s) {
    auto const was_offline = !policy_.IsServerLocallyOnline(s.id, now_);
    s.in_flight = false;
    policy_.ConfirmServerPong(s.id, s.inflight_send, now_, s.inflight_interval,
                              s.inflight_window);
    ++counters_.confirmed_pongs;
    if (was_offline) {
      ++counters_.recoveries_to_online;
    }
    auto* presence = policy_.FindServerPresence(s.id);
    auto const plan = PlanAfterSuccessfulPong(
        presence->confirmed_window_open_local, now_, s.schedule_rtt,
        presence->config_change_pending);
    s.next_due = plan.when;
    s.next_kind = plan.kind;
  }

  void CompleteTimeout(SimServer& s) {
    s.in_flight = false;
    ++s.active_attempt_id;
    auto* presence = policy_.FindServerPresence(s.id);
    if (presence != nullptr) {
      presence->current_attempt_id = s.active_attempt_id;
    }
    ++counters_.timeouts;
    auto const plan = PlanAfterFailedAttempt(
        presence != nullptr && presence->has_confirmed_schedule,
        presence != nullptr ? presence->confirmed_window_open_local : TimePoint{},
        presence != nullptr ? presence->confirmed_window_close_local : TimePoint{},
        s.inflight_kind, now_, s.schedule_rtt);
    if (plan.mark_offline && presence != nullptr) {
      policy_.MarkServerOffline(s.id, now_);
    }
    s.next_due = plan.when;
    s.next_kind = plan.kind;
  }

  ClientConnectivityPolicy policy_;
  TimePoint now_{};
  std::map<ServerId, SimServer> servers_;
  std::vector<AttemptLog> attempts_;
  SimCounters counters_{};
  PollStats poll_stats_{};
};

void test_SendWithoutPongDoesNotConfirm() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100));
  rt.server(sid).connectivity_ok = false;
  rt.AdvanceTo(Tp(0));
  TEST_ASSERT_EQUAL(1, rt.counters().initial);
  TEST_ASSERT_FALSE(rt.IsLocallyOnline());
  TEST_ASSERT_FALSE(rt.policy().FindServerPresence(sid)->has_confirmed_schedule);
}

void test_Prefix1SuccessCancelsPrefix2() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto* st = rt.policy().FindServerPresence(sid);
  TEST_ASSERT_EQUAL(1050, ToMs(st->confirmed_window_open_local));
  auto const prefix1 = ComputePrefix1Time(st->confirmed_window_open_local, Dur(100));
  auto const prefix2 = ComputePrefix2Time(st->confirmed_window_open_local, Dur(100));
  TEST_ASSERT_EQUAL(870, ToMs(prefix1));
  TEST_ASSERT_EQUAL(970, ToMs(prefix2));

  rt.AdvanceTo(prefix1);
  TEST_ASSERT_EQUAL(1, rt.counters().prefix1);
  rt.AdvanceTo(prefix1 + Dur(20));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  TEST_ASSERT_EQUAL(0, rt.counters().prefix2);
  rt.AdvanceTo(prefix2 + Dur(5));
  TEST_ASSERT_EQUAL(0, rt.counters().prefix2);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_Prefix1FailSendsPrefix2OnTarget() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  auto const prefix1 = ComputePrefix1Time(st->confirmed_window_open_local, Dur(100));
  auto const prefix2 = ComputePrefix2Time(st->confirmed_window_open_local, Dur(100));
  rt.server(sid).fail_next_attempts = 1;
  rt.AdvanceTo(prefix1);
  TEST_ASSERT_EQUAL(1, rt.counters().prefix1);
  TEST_ASSERT_EQUAL(0, rt.counters().prefix2);
  rt.AdvanceTo(prefix1 + Dur(100));
  TEST_ASSERT_EQUAL(1, rt.counters().prefix2);
  TEST_ASSERT_EQUAL(970, ToMs(rt.attempts().back().send_time));
  TEST_ASSERT_EQUAL(prefix2.time_since_epoch().count(),
                    rt.attempts().back().send_time.time_since_epoch().count());
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_Prefix2SuccessNoOffline() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  auto const prefix1 = ComputePrefix1Time(st->confirmed_window_open_local, Dur(100));
  rt.server(sid).fail_next_attempts = 1;
  rt.AdvanceTo(prefix1 + Dur(100));
  TEST_ASSERT_EQUAL(1, rt.counters().prefix2);
  rt.AdvanceTo(prefix1 + Dur(120));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_transitions);
}

void test_PostPrefixRetriesStayOnline() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(400)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  auto const close = st->confirmed_window_close_local;
  rt.server(sid).connectivity_ok = false;
  auto const prefix1 = ComputePrefix1Time(st->confirmed_window_open_local, Dur(100));
  rt.AdvanceTo(prefix1);
  rt.AdvanceTo(close);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  TEST_ASSERT_TRUE(rt.counters().prefix2 >= 1);
  TEST_ASSERT_TRUE(rt.counters().retry >= 1);
}

void test_RealOfflineAfterWindowClose() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  auto const close = st->confirmed_window_close_local;
  rt.server(sid).connectivity_ok = false;
  rt.AdvanceTo(close);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  rt.AdvanceTo(close + Dur(1));
  TEST_ASSERT_FALSE(rt.IsLocallyOnline());
}

void test_RecoveryPongRestoresOnline() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  rt.server(sid).connectivity_ok = false;
  rt.AdvanceTo(st->confirmed_window_close_local + Dur(1));
  TEST_ASSERT_FALSE(rt.IsLocallyOnline());
  rt.server(sid).connectivity_ok = true;
  rt.AdvanceTo(rt.now() + Dur(500));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_RxWindowCloseDoesNotCloseTransport() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)), Dur(100));
  rt.server(sid).pong_delay = Dur(100);
  rt.AdvanceTo(Tp(100));
  auto* st = rt.policy().FindServerPresence(sid);
  rt.server(sid).connectivity_ok = false;
  rt.AdvanceTo(st->confirmed_window_close_local + Dur(50));
  TEST_ASSERT_FALSE(rt.IsLocallyOnline());
  TEST_ASSERT_TRUE(rt.server(sid).transport_open);
}

void test_QuarantineIndependentAndReleaseRestartsPing() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const a{1};
  ServerId const b{2};
  rt.AddServer(a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100), 99);
  rt.AddServer(b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)), Dur(200), 95);
  rt.AdvanceTo(Tp(20));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(a, rt.now()));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(b, rt.now()));

  rt.Quarantine(a);
  TEST_ASSERT_FALSE(rt.policy().IsServerLocallyOnline(a, rt.now()));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(b, rt.now()));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());

  auto const prefix1_before = rt.counters().prefix1;
  rt.AdvanceTo(rt.now() + Dur(200));
  TEST_ASSERT_EQUAL(prefix1_before, rt.counters().prefix1);

  rt.Release(a);
  rt.AdvanceTo(rt.now() + Dur(40));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(a, rt.now()));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

void test_RuntimeConfigChangeKeepsOldUntilPong() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(200)), Dur(100));
  rt.AdvanceTo(Tp(20));
  auto const close_old = rt.policy().FindServerPresence(sid)->confirmed_window_close_local;
  rt.policy().ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(10000)).WithWindow(Dur(200)));
  TEST_ASSERT_EQUAL(1000, ToMs(rt.policy().FindServerPresence(sid)->confirmed_interval));
  TEST_ASSERT_TRUE(rt.policy().FindServerPresence(sid)->confirmed_window_close_local ==
                   close_old);
  rt.server(sid).connectivity_ok = false;
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

struct RuntimeReport {
  int confirmed_cycles{};
  Duration duration{};
  TimePoint fault_time{};
  TimePoint confirmed_window_close{};
  TimePoint detected_offline_time{};
  TimePoint recovered_time{};
  bool early_offline{false};
};

RuntimeReport g_stat_report{};
RuntimeReport g_fault_report{};

void test_StatisticalRuntimePollingIsLocallyOnline() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  auto const interval = Dur(1000);
  auto const window = Dur(1000);
  auto const rtt = Dur(100);
  rt.AddServer(sid, RxTimingConf::Every(interval).WithWindow(window), rtt, 99);
  rt.server(sid).pong_delay = Dur(20);

  rt.AdvanceTo(Tp(20));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto const measure_start = rt.now();
  auto const min_duration = Dur(300000);
  constexpr int kMinCycles = 300;

  while (true) {
    rt.AdvancePolling(Dur(10), Dur(10), true);
    auto const elapsed_ms =
        std::chrono::duration_cast<Ms>(rt.now() - measure_start).count();
    if (elapsed_ms >= 300000 && rt.counters().confirmed_pongs >= kMinCycles) {
      break;
    }
    TEST_ASSERT_TRUE(elapsed_ms < 400000);
  }

  g_stat_report.confirmed_cycles = rt.counters().confirmed_pongs;
  g_stat_report.duration =
      std::chrono::duration_cast<Duration>(rt.now() - measure_start);

  std::printf(
      "STATISTICAL runtime\n"
      "  duration_ms=%lld confirmed_pongs=%d status_polls=%d online_samples=%d\n"
      "  false_offline_samples=%d false_offline_transitions=%d "
      "max_false_offline_ms=%lld\n"
      "  prefix1=%d prefix2=%d post_prefix_retry=%d timeouts=%d recoveries=%d\n"
      "  rtt_percentile=99 selected_rtt_ms=%lld guard_ms=30\n",
      static_cast<long long>(ToMs(g_stat_report.duration)),
      rt.counters().confirmed_pongs, rt.poll_stats().status_poll_count,
      rt.poll_stats().online_samples, rt.poll_stats().false_offline_samples,
      rt.poll_stats().false_offline_transitions,
      static_cast<long long>(ToMs(rt.poll_stats().max_false_offline_duration)),
      rt.counters().prefix1, rt.counters().prefix2, rt.counters().retry,
      rt.counters().timeouts, rt.counters().recoveries_to_online,
      static_cast<long long>(ToMs(rtt)));

  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_samples);
  TEST_ASSERT_EQUAL(0, rt.poll_stats().false_offline_transitions);
  TEST_ASSERT_TRUE(rt.counters().confirmed_pongs >= kMinCycles);
  TEST_ASSERT_TRUE(rt.counters().prefix2 == 0);
}

void test_FaultOfflineNotBeforeWindowCloseThenRecovery() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const sid{1};
  rt.AddServer(sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)), Dur(100));
  rt.server(sid).pong_delay = Dur(20);
  rt.AdvanceTo(Tp(20));
  rt.AdvancePolling(Dur(2000), Dur(10), true);
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto* st = rt.policy().FindServerPresence(sid);
  auto const close = st->confirmed_window_close_local;
  g_fault_report.fault_time = rt.now();
  g_fault_report.confirmed_window_close = close;
  rt.server(sid).connectivity_ok = false;

  auto detected = TimePoint{};
  while (rt.now() < close + Dur(2000)) {
    rt.AdvancePolling(Dur(10), Dur(10), false);
    if (!rt.IsLocallyOnline()) {
      detected = rt.now();
      g_fault_report.early_offline = !(detected > close);
      break;
    }
  }
  g_fault_report.detected_offline_time = detected;
  TEST_ASSERT_FALSE(g_fault_report.early_offline);
  TEST_ASSERT_TRUE(detected > close);
  TEST_ASSERT_TRUE(ToMs(detected) >= ToMs(close));

  rt.server(sid).connectivity_ok = true;
  auto const recover_from = rt.now();
  while (rt.now() < recover_from + Dur(2000)) {
    rt.AdvancePolling(Dur(10), Dur(10), false);
    if (rt.IsLocallyOnline()) {
      g_fault_report.recovered_time = rt.now();
      break;
    }
  }
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
  auto const recovery_ms = ToMs(g_fault_report.recovered_time) - ToMs(recover_from);
  std::printf(
      "FAULT runtime\n"
      "  fault_ms=%lld window_close_ms=%lld detected_offline_ms=%lld\n"
      "  early_offline=%s recovery_latency_ms=%lld\n",
      static_cast<long long>(ToMs(g_fault_report.fault_time)),
      static_cast<long long>(ToMs(close)),
      static_cast<long long>(ToMs(detected)),
      g_fault_report.early_offline ? "YES" : "NO",
      static_cast<long long>(recovery_ms));
}

void test_MultiServerRuntimeIndependence() {
  LocalPresenceRuntime rt{Tp(0)};
  ServerId const a{1};
  ServerId const b{2};
  rt.AddServer(a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)), Dur(100), 99);
  rt.AddServer(b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)), Dur(200), 95);
  rt.AdvanceTo(Tp(40));
  auto* sa = rt.policy().FindServerPresence(a);
  auto* sb = rt.policy().FindServerPresence(b);
  TEST_ASSERT_EQUAL(1000, ToMs(sa->confirmed_interval));
  TEST_ASSERT_EQUAL(3000, ToMs(sb->confirmed_interval));
  TEST_ASSERT_EQUAL(300, ToMs(sa->confirmed_rx_window));
  TEST_ASSERT_EQUAL(700, ToMs(sb->confirmed_rx_window));
  auto const p1a = ComputePrefix1Time(sa->confirmed_window_open_local, Dur(100));
  auto const p1b = ComputePrefix1Time(sb->confirmed_window_open_local, Dur(200));
  TEST_ASSERT_TRUE(ToMs(p1a) != ToMs(p1b));
  rt.server(a).connectivity_ok = false;
  rt.AdvanceTo(sa->confirmed_window_close_local + Dur(1));
  TEST_ASSERT_FALSE(rt.policy().IsServerLocallyOnline(a, rt.now()));
  TEST_ASSERT_TRUE(rt.policy().IsServerLocallyOnline(b, rt.now()));
  TEST_ASSERT_TRUE(rt.IsLocallyOnline());
}

}  // namespace ae::test_local_presence

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_local_presence::test_PrefixFormula);
  RUN_TEST(ae::test_local_presence::test_ConfirmOnlyAfterPong);
  RUN_TEST(ae::test_local_presence::test_PerServerIndependence);
  RUN_TEST(ae::test_local_presence::test_OfflineOnlyAfterWindowClose);
  RUN_TEST(ae::test_local_presence::test_RuntimeIntervalChangeKeepsOldConfirmed);
  RUN_TEST(ae::test_local_presence::test_RecoveryAfterOffline);
  RUN_TEST(ae::test_local_presence::test_RuntimePercentile);
  RUN_TEST(ae::test_local_presence::test_ReliabilityP95VsP99PrefixTimes);
  RUN_TEST(ae::test_local_presence::test_AggregateIgnoresDeselected);
  RUN_TEST(ae::test_local_presence::test_OneWayProjection);
  RUN_TEST(ae::test_local_presence::test_MakeConfirmedScheduleDeterministic);
  RUN_TEST(ae::test_local_presence::test_PlanPrefix1FailSchedulesPrefix2);
  RUN_TEST(ae::test_local_presence::test_PlanPrefix2FailRetriesWhileOnline);
  RUN_TEST(ae::test_local_presence::test_PlanAfterCloseIsRecoveryOffline);
  RUN_TEST(ae::test_local_presence::test_StaleAttemptRejected);
  RUN_TEST(ae::test_local_presence::test_PlanSuccessSchedulesPrefix1);
  RUN_TEST(ae::test_local_presence::test_SendWithoutPongDoesNotConfirm);
  RUN_TEST(ae::test_local_presence::test_Prefix1SuccessCancelsPrefix2);
  RUN_TEST(ae::test_local_presence::test_Prefix1FailSendsPrefix2OnTarget);
  RUN_TEST(ae::test_local_presence::test_Prefix2SuccessNoOffline);
  RUN_TEST(ae::test_local_presence::test_PostPrefixRetriesStayOnline);
  RUN_TEST(ae::test_local_presence::test_RealOfflineAfterWindowClose);
  RUN_TEST(ae::test_local_presence::test_RecoveryPongRestoresOnline);
  RUN_TEST(ae::test_local_presence::test_RxWindowCloseDoesNotCloseTransport);
  RUN_TEST(ae::test_local_presence::test_QuarantineIndependentAndReleaseRestartsPing);
  RUN_TEST(ae::test_local_presence::test_RuntimeConfigChangeKeepsOldUntilPong);
  RUN_TEST(ae::test_local_presence::test_StatisticalRuntimePollingIsLocallyOnline);
  RUN_TEST(ae::test_local_presence::test_FaultOfflineNotBeforeWindowCloseThenRecovery);
  RUN_TEST(ae::test_local_presence::test_MultiServerRuntimeIndependence);
  return UNITY_END();
}
