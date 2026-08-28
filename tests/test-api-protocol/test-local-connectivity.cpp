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
#include "aether/cloud_connections/ping_schedule_guard.h"
#include "aether/receive_schedule.h"

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

}  // namespace

// Covers local online/offline classification from last successful cloud ping.
void test_IsLocallyOnlineInsideAndOutsideReceiveWindow() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));

  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(5000)));

  policy.ReportSuccessfulCloudResponse(Tp(1000));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(2500)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3999)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(4000)));
}

void test_ResetRxTimingsClearsLastSuccessfulResponse() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));
  policy.ReportSuccessfulCloudResponse(Tp(1000));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(2000)));

  policy.ResetRxTimings();
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(2000)));
}

// A new pong before expiry extends the online window without going offline.
void test_NewPongBeforeExpiryExtendsOnlineWindow() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));

  policy.ReportSuccessfulCloudResponse(Tp(0));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(2999)));

  policy.ReportSuccessfulCloudResponse(Tp(2500));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(5499)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(5500)));
}

// In-flight ping keeps local online across receive-window boundary.
void test_InFlightPingPreventsBoundaryOffline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(3000, 3000));

  policy.ReportSuccessfulCloudResponse(Tp(50));
  policy.ReportPingDispatched(Tp(2990), Ms(200));

  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3050)));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3100)));

  policy.ReportSuccessfulCloudResponse(Tp(3040));
  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(3050)));
}

// Regular 1s successes with 3s window never go offline between pongs.
void test_ScheduledPingBoundaryNeverFalseOffline() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));

  policy.ReportSuccessfulCloudResponse(Tp(40));
  for (std::uint32_t second = 1; second < 10; ++second) {
    auto const pong = Tp(second * 1000 + 40);
    for (std::uint32_t probe = (second - 1) * 1000 + 41;
         probe <= second * 1000 + 39; ++probe) {
      TEST_ASSERT_TRUE_MESSAGE(
          policy.IsLocallyOnline(Tp(probe)),
          "false offline during stable ping cadence");
    }
    policy.ReportSuccessfulCloudResponse(pong);
  }
}

// Real miss: no success for longer than receive_window goes offline.
void test_RealMissGoesOfflineAfterReceiveWindow() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));

  policy.ReportSuccessfulCloudResponse(Tp(0));
  policy.ReportPingDispatched(Tp(1000), Ms(200));
  policy.ReportPingCompletedWithoutSuccess(Tp(1200));

  TEST_ASSERT_TRUE(policy.IsLocallyOnline(Tp(2999)));
  TEST_ASSERT_FALSE(policy.IsLocallyOnline(Tp(3000)));
}

void test_LocalOnlineUntilUsesRxWindowCloseHelper() {
  ClientConnectivityPolicy policy;
  policy.ConfigureRxTimings().ForAllPriorities(Timing(1000, 3000));

  auto const pong = Tp(1000);
  policy.ReportSuccessfulCloudResponse(pong);
  TEST_ASSERT_TRUE(policy.local_online_until() ==
                   ComputeRxWindowCloseTime(pong, Ms(3000)));
}

}  // namespace ae::test_local_connectivity

int test_local_connectivity() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_local_connectivity::
               test_IsLocallyOnlineInsideAndOutsideReceiveWindow);
  RUN_TEST(
      ae::test_local_connectivity::test_ResetRxTimingsClearsLastSuccessfulResponse);
  RUN_TEST(ae::test_local_connectivity::test_NewPongBeforeExpiryExtendsOnlineWindow);
  RUN_TEST(ae::test_local_connectivity::test_InFlightPingPreventsBoundaryOffline);
  RUN_TEST(ae::test_local_connectivity::test_ScheduledPingBoundaryNeverFalseOffline);
  RUN_TEST(ae::test_local_connectivity::test_RealMissGoesOfflineAfterReceiveWindow);
  RUN_TEST(ae::test_local_connectivity::test_LocalOnlineUntilUsesRxWindowCloseHelper);
  return UNITY_END();
}
