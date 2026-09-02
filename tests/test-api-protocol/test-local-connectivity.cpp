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

#include <unity.h>

#include <chrono>

#include "aether/client_connectivity_policy.h"

namespace ae::test_local_connectivity {
namespace {

Duration Ms(std::uint32_t v) {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{v});
}

TimePoint Tp(std::uint32_t ms) {
  return TimePoint{Ms(ms)};
}

RxTimingConf Timing(std::uint32_t interval_ms, std::uint32_t window_ms) {
  return RxTimingConf{.interval = Ms(interval_ms), .rx_window = Ms(window_ms)};
}

void Success(ClientConnectivityPolicy& policy, TimePoint at,
             std::uint32_t interval_ms = 1000, std::size_t priority = 0) {
  policy.ReportSuccessfulCloudResponse(at, Ms(interval_ms), priority);
}

}  // namespace

void test_NoSuccessfulResponseIsOffline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(0)));
}

void test_TimeSinceLastResponseIsReturnedExactly() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(1000));
  auto const age = policy.TimeSinceLastSuccessfulCloudResponse(Tp(2500));
  TEST_ASSERT_TRUE(age.has_value());
  TEST_ASSERT_EQUAL_UINT(1500, static_cast<unsigned>(age->count()));
}

void test_AgeBelowPingIntervalIsOnline() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(1000), 1000);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1999)));
}

void test_AgeAtOrAbovePingIntervalIsOffline() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(1000), 1000);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
}

void test_ReceiveWindowDoesNotAffectLocalOnline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 10000));
  Success(policy, Tp(0), 1000);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(999)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(1000)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(5000)));
}

void test_InFlightPingBridgesPingIntervalBoundary() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(0), 1000);
  policy.ReportPingDispatched(Tp(990), Ms(200), 0);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1050)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1189)));
}

void test_PingDispatchedAfterAlreadyOfflineDoesNotReviveClient() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(0), 1000);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
  policy.ReportPingDispatched(Tp(2000), Ms(200), 0);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2010)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2199)));
}

void test_FailedPingEndsGrace() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(0), 1000);
  policy.ReportPingDispatched(Tp(990), Ms(200), 0);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1050)));
  policy.ReportPingCompletedWithoutSuccess(Tp(1100), 0);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(1100)));
}

void test_SuccessfulPingResetsAge() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(0), 1000);
  Success(policy, Tp(2500), 1000);
  auto const age = policy.TimeSinceLastSuccessfulCloudResponse(Tp(3000));
  TEST_ASSERT_TRUE(age.has_value());
  TEST_ASSERT_EQUAL_UINT(500, static_cast<unsigned>(age->count()));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3499)));
}

void test_Interval1sWindow1sStablePingsDoNotFlicker() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  Success(policy, Tp(40), 1000);
  for (std::uint32_t second = 1; second < 10; ++second) {
    for (std::uint32_t probe = (second - 1) * 1000 + 41;
         probe < second * 1000 + 40; ++probe) {
      TEST_ASSERT_TRUE_MESSAGE(policy.IsLocallyOnline(Tp(probe)),
                               "false offline during stable ping cadence");
    }
    Success(policy, Tp(second * 1000 + 40), 1000);
  }
}

void test_Interval1sWindow3sHasSameLocalOnlineSemantics() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));
  Success(policy, Tp(40), 1000);
  for (std::uint32_t second = 1; second < 10; ++second) {
    for (std::uint32_t probe = (second - 1) * 1000 + 41;
         probe < second * 1000 + 40; ++probe) {
      TEST_ASSERT_TRUE_MESSAGE(policy.IsLocallyOnline(Tp(probe)),
                               "window must not affect local online");
    }
    Success(policy, Tp(second * 1000 + 40), 1000);
  }
}

void test_MultiplePrioritiesDoNotLeaveStaleGraceDeadline() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(0), 1000, 0);
  policy.ReportPingDispatched(Tp(990), Ms(5000), 0);
  policy.ReportPingCompletedWithoutSuccess(Tp(1100), 0);
  policy.ReportPingDispatched(Tp(2000), Ms(200), 1);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
  auto snap = policy.InspectLocalConnectivity(Tp(2100));
  TEST_ASSERT_FALSE(snap.in_flight_grace_active);
  TEST_ASSERT_EQUAL_UINT(1, snap.pings_in_flight);
}

void test_ResetRxTimingsClearsAllLocalConnectivityState() {
  ClientConnectivityPolicy policy;
  Success(policy, Tp(1000), 1000);
  policy.ReportPingDispatched(Tp(1500), Ms(200), 0);
  policy.ResetRxTimings();
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
  TEST_ASSERT_FALSE(policy.TimeSinceLastSuccessfulCloudResponse(Tp(2000))
                        .has_value());
  auto snap = policy.InspectLocalConnectivity(Tp(2000));
  TEST_ASSERT_EQUAL_UINT(0, snap.pings_in_flight);
}

void test_ClockRollbackClearsRuntimeState() {
  ClientConnectivityPolicy policy;
  auto const far_future = TimePoint{std::chrono::hours(24 * 365 * 100)};
  Success(policy, far_future, 1000);
  policy.ReportPingDispatched(far_future + Ms(100), Ms(200), 0);
  policy.ResetRuntimeState();
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(1000)));
  TEST_ASSERT_FALSE(
      policy.TimeSinceLastSuccessfulCloudResponse(Tp(1000)).has_value());
  auto snap = policy.InspectLocalConnectivity(Tp(1000));
  TEST_ASSERT_EQUAL_UINT(0, snap.pings_in_flight);
}

}  // namespace ae::test_local_connectivity

int test_local_connectivity() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_local_connectivity::test_NoSuccessfulResponseIsOffline);
  RUN_TEST(ae::test_local_connectivity::test_TimeSinceLastResponseIsReturnedExactly);
  RUN_TEST(ae::test_local_connectivity::test_AgeBelowPingIntervalIsOnline);
  RUN_TEST(ae::test_local_connectivity::test_AgeAtOrAbovePingIntervalIsOffline);
  RUN_TEST(ae::test_local_connectivity::test_ReceiveWindowDoesNotAffectLocalOnline);
  RUN_TEST(
      ae::test_local_connectivity::test_InFlightPingBridgesPingIntervalBoundary);
  RUN_TEST(ae::test_local_connectivity::
               test_PingDispatchedAfterAlreadyOfflineDoesNotReviveClient);
  RUN_TEST(ae::test_local_connectivity::test_FailedPingEndsGrace);
  RUN_TEST(ae::test_local_connectivity::test_SuccessfulPingResetsAge);
  RUN_TEST(
      ae::test_local_connectivity::test_Interval1sWindow1sStablePingsDoNotFlicker);
  RUN_TEST(ae::test_local_connectivity::
               test_Interval1sWindow3sHasSameLocalOnlineSemantics);
  RUN_TEST(ae::test_local_connectivity::
               test_MultiplePrioritiesDoNotLeaveStaleGraceDeadline);
  RUN_TEST(ae::test_local_connectivity::
               test_ResetRxTimingsClearsAllLocalConnectivityState);
  RUN_TEST(ae::test_local_connectivity::test_ClockRollbackClearsRuntimeState);
  return UNITY_END();
}
