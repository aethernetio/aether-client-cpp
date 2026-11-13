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

#include "ae-numeric/percentile.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/remote_presence.h"
#include "aether/types/statistic_counter.h"
#include "aether/work_cloud_api/client_timing.h"

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

void test_ConfirmOnlyAfterPong() {
  ClientConnectivityPolicy policy;
  ServerId const sid{7};
  policy.ConfigureServerRxTiming(
      sid, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
      Percentile::FromPercent(99.0));
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
  auto const deadline = LocalOfflineDeadline(
      state->confirmed_window_open_local, policy.offline_detection_timeout());
  TEST_ASSERT_EQUAL(3050, ToMs(deadline));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, Tp(2050)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(sid, deadline));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(sid, deadline + Dur(1)));
}

void test_LatePongDoesNotAdvanceSelectedRttProjection() {
  auto const selected = Dur(100);
  auto fast =
      MakeConfirmedSchedule(Tp(1000), Tp(1400), Dur(1000), Dur(1000), selected);
  auto slow =
      MakeConfirmedSchedule(Tp(1000), Tp(1900), Dur(1000), Dur(1000), selected);
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
      a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(300)),
      Percentile::FromPercent(99.0));
  policy.ConfigureServerRxTiming(
      b, RxTimingConf::Every(Dur(3000)).WithWindow(Dur(700)),
      Percentile::FromPercent(95.0));
  policy.ConfirmServerPong(a, Tp(0), Tp(100), Dur(1000), Dur(300), Dur(100));
  TEST_ASSERT_FALSE(policy.IsServerLocallyOnline(b, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsServerLocallyOnline(a, Tp(50)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(50)));
}

void test_OfflineOnlyAfterOfflineDetectionTimeout() {
  ClientConnectivityPolicy policy;
  ServerId const sid{3};
  policy.SetOfflineDetectionTimeout(Dur(1000));
  policy.ConfirmServerPong(sid, Tp(0), Tp(40), Dur(1000), Dur(10000), Dur(40));
  // open = 0 + 20 + 1000 = 1020; deadline = 2020 even with rx_window=10s.
  TEST_ASSERT_EQUAL(
      1020, ToMs(policy.FindServerPresence(sid)->confirmed_window_open_local));
  TEST_ASSERT_EQUAL(
      11020,
      ToMs(policy.FindServerPresence(sid)->confirmed_window_close_local));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(2020)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2021)));
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

void test_OneWayProjection() {
  TEST_ASSERT_EQUAL(50, ToMs(OneWayFromRtt(Dur(100))));
}

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
      a, RxTimingConf::Every(Dur(1000)).WithWindow(Dur(1000)),
      Percentile::FromPercent(99.0));
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

void test_RemoteTimingProjectionAndAggregation() {
  ClientTiming timing{};
  timing.next_ping_delta_ms = 500;
  timing.last_connect_delta_ms = 0;
  TimePoint expected{};
  TimePoint deadline{};
  auto status = ClassifyRemoteServerPresence(
      Tp(2550), Tp(1000), Tp(1100), timing, Dur(1000), &expected, &deadline);
  TEST_ASSERT_EQUAL(1550, ToMs(expected));
  TEST_ASSERT_EQUAL(2550, ToMs(deadline));
  TEST_ASSERT_EQUAL(static_cast<int>(RemoteServerPresence::kOnline),
                    static_cast<int>(status));
  status = ClassifyRemoteServerPresence(Tp(2551), Tp(1000), Tp(1100), timing,
                                        Dur(1000), &expected, &deadline);
  TEST_ASSERT_EQUAL(static_cast<int>(RemoteServerPresence::kOffline),
                    static_cast<int>(status));

  timing.next_ping_delta_ms = -200;
  status = ClassifyRemoteServerPresence(Tp(1850), Tp(1000), Tp(1100), timing,
                                        Dur(1000), &expected, &deadline);
  TEST_ASSERT_EQUAL(850, ToMs(expected));
  TEST_ASSERT_EQUAL(1850, ToMs(deadline));
  TEST_ASSERT_EQUAL(static_cast<int>(RemoteServerPresence::kOnline),
                    static_cast<int>(status));
  status = ClassifyRemoteServerPresence(Tp(1851), Tp(1000), Tp(1100), timing,
                                        Dur(1000), &expected, &deadline);
  TEST_ASSERT_EQUAL(static_cast<int>(RemoteServerPresence::kOffline),
                    static_cast<int>(status));

  timing.next_ping_delta_ms = 0;
  status = ClassifyRemoteServerPresence(Tp(0), Tp(1000), Tp(1100), timing,
                                        Dur(1000));
  TEST_ASSERT_EQUAL(static_cast<int>(RemoteServerPresence::kUnknown),
                    static_cast<int>(status));

  std::vector<RemoteServerPresenceSample> samples(3);
  samples[0] = {ServerId{1}, RemoteServerPresence::kOnline, {}, {}, 0, true};
  samples[1] = {ServerId{2}, RemoteServerPresence::kOnline, {}, {}, 0, true};
  samples[2] = {ServerId{3}, RemoteServerPresence::kOnline, {}, {}, 0, true};
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOnline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  samples[1].status = RemoteServerPresence::kOffline;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOffline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  samples[1].status = RemoteServerPresence::kUnknown;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kUnknown),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  samples[1].status = RemoteServerPresence::kExcluded;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOnline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  samples[0].status = RemoteServerPresence::kExcluded;
  samples[1].status = RemoteServerPresence::kExcluded;
  samples[2].status = RemoteServerPresence::kExcluded;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kUnknown),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  TEST_ASSERT_TRUE(RemotePresenceCanEarlyCompleteOffline(
      {{ServerId{1}, RemoteServerPresence::kOffline, {}, {}, 0, true}}));
}

void test_NoObserverCloudFallbackAndAuthoritativeSet() {
  TEST_ASSERT_FALSE(AllowObserverCloudFallbackForPeerPresence());

  // Peer Personal Cloud with 4 servers; Local Presence contract is bounded by
  // AE_CLOUD_MAX_SERVER_CONNECTIONS (default 3) via selected_servers().
  std::vector<ServerId> peer_cloud{10, 11, 12, 13};
  auto const contract = AuthoritativePresenceServerIds(
      peer_cloud, AE_CLOUD_MAX_SERVER_CONNECTIONS);
  TEST_ASSERT_EQUAL(4, peer_cloud.size());
  TEST_ASSERT_EQUAL(AE_CLOUD_MAX_SERVER_CONNECTIONS, contract.size());
  TEST_ASSERT_EQUAL(10, contract[0]);
  TEST_ASSERT_EQUAL(11, contract[1]);
  TEST_ASSERT_EQUAL(12, contract[2]);
  // Server 13 is in peer cloud but outside the Local Presence contract when
  // N > AE_CLOUD_MAX_SERVER_CONNECTIONS — Remote AND must not require it.
  TEST_ASSERT_TRUE(std::find(contract.begin(), contract.end(),
                             static_cast<ServerId>(13)) == contract.end());

  // When N <= max, authoritative set equals the full peer cloud.
  std::vector<ServerId> peer_small{21, 22};
  auto const full = AuthoritativePresenceServerIds(
      peer_small, AE_CLOUD_MAX_SERVER_CONNECTIONS);
  TEST_ASSERT_EQUAL(2, full.size());
  TEST_ASSERT_EQUAL(21, full[0]);
  TEST_ASSERT_EQUAL(22, full[1]);

  // Observer cloud IDs must never be treated as peer Presence substitutes.
  std::vector<ServerId> observer{90, 91};
  auto const observer_contract =
      AuthoritativePresenceServerIds(observer, AE_CLOUD_MAX_SERVER_CONNECTIONS);
  for (auto const id : observer_contract) {
    TEST_ASSERT_TRUE(std::find(contract.begin(), contract.end(), id) ==
                     contract.end());
    TEST_ASSERT_TRUE(std::find(full.begin(), full.end(), id) == full.end());
  }

  // Peer cloud unavailable => aggregate UNKNOWN with zero usable samples.
  // QueryPeerPresence::OnCloud(Error) completes with this result and never
  // binds the observer cloud (used_observer_cloud remains false; queried
  // server list stays empty — no observer servers contacted).
  std::vector<RemoteServerPresenceSample> empty;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kUnknown),
                    static_cast<int>(AggregateRemotePresence(empty).state));
  TEST_ASSERT_EQUAL(0, empty.size());
}

void test_PeerCloudNotObserverCloudAuthoritativeIds() {
  // Deterministic peer vs observer cloud sets (integration contract).
  std::vector<ServerId> peer{101, 102};
  std::vector<ServerId> observer{201, 202};
  auto const auth =
      AuthoritativePresenceServerIds(peer, AE_CLOUD_MAX_SERVER_CONNECTIONS);
  TEST_ASSERT_EQUAL(2, auth.size());
  TEST_ASSERT_EQUAL(101, auth[0]);
  TEST_ASSERT_EQUAL(102, auth[1]);
  for (auto const oid : observer) {
    TEST_ASSERT_TRUE(std::find(auth.begin(), auth.end(), oid) == auth.end());
  }
  // Unknown peer cloud: no authoritative servers => UNKNOWN, no fallback set.
  auto const none =
      AuthoritativePresenceServerIds({}, AE_CLOUD_MAX_SERVER_CONNECTIONS);
  TEST_ASSERT_EQUAL(0, none.size());
  TEST_ASSERT_EQUAL(
      static_cast<int>(PeerPresenceState::kUnknown),
      static_cast<int>(
          AggregateRemotePresence(std::vector<RemoteServerPresenceSample>{})
              .state));
}

void test_RecoveredServerRequiresFreshOnline() {
  std::vector<RemoteServerPresenceSample> samples{
      {1, RemoteServerPresence::kOnline, {}, {}, 1, true},
      {2, RemoteServerPresence::kExcluded, {}, {}, 0, false},
  };
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOnline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  // Recovered server re-enters as UNKNOWN — cannot keep stale ONLINE.
  samples[1].status = RemoteServerPresence::kUnknown;
  samples[1].has_timing = false;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kUnknown),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  samples[1].status = RemoteServerPresence::kOnline;
  samples[1].has_timing = true;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOnline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
}

void test_QueryFailureIsUnknownNotOffline() {
  // Usable server with failed timing (UNKNOWN after retries) must not force
  // peer Offline; zero Offline contributions + incomplete ONLINE => UNKNOWN.
  std::vector<RemoteServerPresenceSample> samples{
      {1, RemoteServerPresence::kOnline, {}, {}, 1, true},
      {2, RemoteServerPresence::kUnknown, {}, {}, 0, true},
  };
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kUnknown),
                    static_cast<int>(AggregateRemotePresence(samples).state));
  // After quarantine/unselect, remaining ONLINE servers may aggregate ONLINE.
  samples[1].status = RemoteServerPresence::kExcluded;
  TEST_ASSERT_EQUAL(static_cast<int>(PeerPresenceState::kOnline),
                    static_cast<int>(AggregateRemotePresence(samples).state));
}
void test_StaleRttCannotProjectServerReceiptAfterAck() {
  ClientConnectivityPolicy policy;
  policy.SetOfflineDetectionTimeout(Dur(1000));
  policy.ConfigureServerRxTiming(7, RxTimingConf::Every(Dur(1000)));
  // Earlier blackout left a 60s RTT tail; this Ping was acknowledged in 100ms.
  policy.ConfirmServerPong(7, Tp(1000), Tp(1100), Dur(1000), Dur(1000),
                           Dur(60000));
  auto const* state = policy.FindServerPresence(7);
  TEST_ASSERT_NOT_NULL(state);
  TEST_ASSERT_EQUAL(2100, ToMs(state->confirmed_window_open_local));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3100)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(3101)));
}

}  // namespace ae::test_local_presence
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_local_presence::test_StaleRttCannotProjectServerReceiptAfterAck);
  RUN_TEST(ae::test_local_presence::test_ConfirmOnlyAfterPong);
  RUN_TEST(
      ae::test_local_presence::test_LatePongDoesNotAdvanceSelectedRttProjection);
  RUN_TEST(ae::test_local_presence::test_PerServerIndependence);
  RUN_TEST(
      ae::test_local_presence::test_OfflineOnlyAfterOfflineDetectionTimeout);
  RUN_TEST(
      ae::test_local_presence::test_RuntimeIntervalChangeKeepsOldConfirmed);
  RUN_TEST(ae::test_local_presence::test_RuntimePercentile);
  RUN_TEST(ae::test_local_presence::test_AggregateIgnoresDeselected);
  RUN_TEST(ae::test_local_presence::test_OneWayProjection);
  RUN_TEST(ae::test_local_presence::test_MakeConfirmedScheduleDeterministic);
  RUN_TEST(ae::test_local_presence::test_ConfigScopeOverrideAndPriority);
  RUN_TEST(ae::test_local_presence::test_RemoteTimingProjectionAndAggregation);
  RUN_TEST(
      ae::test_local_presence::test_NoObserverCloudFallbackAndAuthoritativeSet);
  RUN_TEST(
      ae::test_local_presence::test_PeerCloudNotObserverCloudAuthoritativeIds);
  RUN_TEST(ae::test_local_presence::test_RecoveredServerRequiresFreshOnline);
  RUN_TEST(ae::test_local_presence::test_QueryFailureIsUnknownNotOffline);
  return UNITY_END();
}
