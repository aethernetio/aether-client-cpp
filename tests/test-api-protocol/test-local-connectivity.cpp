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
  return TimePoint{} + std::chrono::milliseconds{ms};
}

std::uint32_t AgeMs(Duration d) {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

RxTimingConf Timing(std::uint32_t interval_ms, std::uint32_t window_ms) {
  return RxTimingConf{.interval = Ms(interval_ms), .rx_window = Ms(window_ms)};
}

void AnyCloud(ClientConnectivityPolicy& policy, TimePoint at,
              std::size_t priority = 0) {
  policy.ReportAuthenticatedCloudResponse(priority, at);
}

LocalConnectivityState StateAt(ClientConnectivityPolicy& policy,
                               TimePoint now) {
  return policy.InspectLocalConnectivity(now).state;
}

}  // namespace

void test_NoSuccessfulResponseIsWaiting() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(0)) ==
                   LocalConnectivityState::kWaitingFirstResponse);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(0)));
}

void test_GenericCloudResponseUpdatesAnyNotPing() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  AnyCloud(policy, Tp(1000));
  policy.ReportPingDispatched(0, 1, Tp(1990), Ms(200));
  auto snap = policy.InspectLocalConnectivity(Tp(2000));
  TEST_ASSERT_TRUE(snap.has_any_cloud_response);
  TEST_ASSERT_FALSE(snap.has_ping_response);
  TEST_ASSERT_EQUAL_UINT(1, snap.pings_in_flight);
}

void test_SuccessfulPingUpdatesBothAndClearsFlight() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  AnyCloud(policy, Tp(0));
  policy.ReportPingDispatched(0, 1, Tp(990), Ms(200));
  policy.ReportSuccessfulPingResponse(0, 1, Tp(1005));
  auto snap = policy.InspectLocalConnectivity(Tp(1010));
  TEST_ASSERT_TRUE(snap.has_ping_response);
  TEST_ASSERT_EQUAL_UINT(0, snap.pings_in_flight);
}

void test_OldPingCallbackIgnored() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  AnyCloud(policy, Tp(0));
  policy.ReportPingDispatched(0, 2, Tp(990), Ms(200));
  policy.ReportSuccessfulPingResponse(0, 1, Tp(995));
  TEST_ASSERT_EQUAL_UINT(1, policy.InspectLocalConnectivity(Tp(1000)).pings_in_flight);
}

void test_TimePointEpochWithHasValueBool() {
  ClientConnectivityPolicy policy;
  auto snap = policy.InspectLocalConnectivity(Tp(0));
  TEST_ASSERT_FALSE(snap.has_any_cloud_response);
  TEST_ASSERT_FALSE(snap.has_ping_response);
  TEST_ASSERT_EQUAL_INT64(0, snap.last_any_cloud_response.time_since_epoch().count());
}

void test_AgeBelowPingIntervalIsOnline() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(1000));
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 10000));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1999)));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1999)) == LocalConnectivityState::kOnline);
}

void test_FirstIntervalMissIsSuspectNotOffline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 10000));
  AnyCloud(policy, Tp(0));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1000)) == LocalConnectivityState::kSuspect);
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1000)));
}

void test_SecondIntervalMissIsOffline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 10000));
  AnyCloud(policy, Tp(0));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(2000)) == LocalConnectivityState::kOffline);
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
}

void test_NewAuthenticatedResponseRestoresOnline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  AnyCloud(policy, Tp(0));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1500)) == LocalConnectivityState::kSuspect);
  AnyCloud(policy, Tp(1500));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1501)) == LocalConnectivityState::kOnline);
}

void test_ReceiveWindowDoesNotAffectLocalOnline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 10000));
  AnyCloud(policy, Tp(0));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(999)));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1500)) == LocalConnectivityState::kSuspect);
  TEST_ASSERT_TRUE(StateAt(policy, Tp(5000)) == LocalConnectivityState::kOffline);
}

void test_InFlightPingBridgesPingIntervalBoundary() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(0));
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  policy.ReportPingDispatched(0, 1, Tp(990), Ms(200));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1050)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1189)));
}

void test_PingDispatchedAfterAlreadyOfflineDoesNotReviveClient() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  AnyCloud(policy, Tp(0));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2001)));
  policy.ReportPingDispatched(0, 1, Tp(2001), Ms(200));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2010)));
}

void test_FailedPingEndsGrace() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(0));
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  policy.ReportPingDispatched(0, 1, Tp(990), Ms(200));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(1050)));
  policy.ReportPingCompletedWithoutSuccess(0, 1, Tp(1100));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1101)) == LocalConnectivityState::kSuspect);
}

void test_PlannedPingHoldsOnlineAcrossBoundary() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(0));
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  policy.ReportPingPlanned(0, 1, Tp(900), Tp(1100),
                           LocalConnectivityState::kOnline);
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1050)) == LocalConnectivityState::kOnline);
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1101)) == LocalConnectivityState::kSuspect);
}

void test_PlannedPingInSuspectDoesNotUpgradeToOnline() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(0));
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 1000));
  policy.ReportPingPlanned(0, 1, Tp(1000), Tp(1300),
                           LocalConnectivityState::kSuspect);
  TEST_ASSERT_TRUE(StateAt(policy, Tp(1200)) == LocalConnectivityState::kSuspect);
}

void test_TwoPrioritiesAggregateOnline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings()
      .ForPriority<0>(Timing(1000, 1000))
      .ForPriority<1>(Timing(2000, 1000));
  AnyCloud(policy, Tp(0), 1);
  AnyCloud(policy, Tp(2500), 0);
  TEST_ASSERT_TRUE(StateAt(policy, Tp(2600)) == LocalConnectivityState::kOnline);
  policy.ReportPingDispatched(0, 1, Tp(2590), Ms(200));
  policy.ReportPingDispatched(1, 2, Tp(2590), Ms(200));
  policy.ReportPingCompletedWithoutSuccess(0, 1, Tp(2600));
  TEST_ASSERT_TRUE(StateAt(policy, Tp(2601)) == LocalConnectivityState::kOnline);
}

void test_ResetRxTimingsClearsAllLocalConnectivityState() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(1000));
  policy.ReportPingDispatched(0, 1, Tp(1500), Ms(200));
  policy.ResetRxTimings();
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
  TEST_ASSERT_FALSE(policy.TimeSinceLastSuccessfulCloudResponse(Tp(2000))
                        .has_value());
}

void test_ResetRuntimeStateClearsRuntimeOnly() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(1000));
  policy.ResetRuntimeState();
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
}

void test_TimeSinceLastResponseIsReturnedExactly() {
  ClientConnectivityPolicy policy;
  AnyCloud(policy, Tp(1000));
  auto const age = policy.TimeSinceLastSuccessfulCloudResponse(Tp(2500));
  TEST_ASSERT_TRUE(age.has_value());
  TEST_ASSERT_EQUAL_UINT(1500, AgeMs(*age));
}

}  // namespace ae::test_local_connectivity

int test_local_connectivity() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_local_connectivity::test_NoSuccessfulResponseIsWaiting);
  RUN_TEST(ae::test_local_connectivity::test_GenericCloudResponseUpdatesAnyNotPing);
  RUN_TEST(ae::test_local_connectivity::test_SuccessfulPingUpdatesBothAndClearsFlight);
  RUN_TEST(ae::test_local_connectivity::test_OldPingCallbackIgnored);
  RUN_TEST(ae::test_local_connectivity::test_TimePointEpochWithHasValueBool);
  RUN_TEST(ae::test_local_connectivity::test_AgeBelowPingIntervalIsOnline);
  RUN_TEST(ae::test_local_connectivity::test_FirstIntervalMissIsSuspectNotOffline);
  RUN_TEST(ae::test_local_connectivity::test_SecondIntervalMissIsOffline);
  RUN_TEST(ae::test_local_connectivity::test_NewAuthenticatedResponseRestoresOnline);
  RUN_TEST(ae::test_local_connectivity::test_ReceiveWindowDoesNotAffectLocalOnline);
  RUN_TEST(ae::test_local_connectivity::test_InFlightPingBridgesPingIntervalBoundary);
  RUN_TEST(ae::test_local_connectivity::test_PingDispatchedAfterAlreadyOfflineDoesNotReviveClient);
  RUN_TEST(ae::test_local_connectivity::test_FailedPingEndsGrace);
  RUN_TEST(ae::test_local_connectivity::test_PlannedPingHoldsOnlineAcrossBoundary);
  RUN_TEST(ae::test_local_connectivity::test_PlannedPingInSuspectDoesNotUpgradeToOnline);
  RUN_TEST(ae::test_local_connectivity::test_TwoPrioritiesAggregateOnline);
  RUN_TEST(ae::test_local_connectivity::test_ResetRxTimingsClearsAllLocalConnectivityState);
  RUN_TEST(ae::test_local_connectivity::test_ResetRuntimeStateClearsRuntimeOnly);
  RUN_TEST(ae::test_local_connectivity::test_TimeSinceLastResponseIsReturnedExactly);
  return UNITY_END();
}
