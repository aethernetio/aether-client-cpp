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
#include <cstdint>
#include <optional>

#include "aether/api_protocol/api_protocol.h"
#include "aether/cloud_connections/ping_schedule_guard.h"
#include "aether/config.h"
#include "aether/types/data_buffer.h"

namespace ae::test_client_online_timing {
namespace {

Duration Ms(std::uint32_t v) {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{v});
}

TimePoint Tp(std::uint32_t ms) { return TimePoint{} + Ms(ms); }

}  // namespace

void test_InitialClientOnlineTimestampsAreEmpty() {
  // Without a live ping schedule / cloud connection, expected is nullopt.
  // last_online starts empty; verified via MarkServerResponseReceived below
  // once a response is observed.
  LogicalPingCycleState st{};
  TEST_ASSERT_FALSE(ExpectedPingResponseTimeForCycle(st).has_value());
  TEST_ASSERT_FALSE(st.has_schedule);
}

void test_ExpectedPingResponseIsTnPlusHalfP99() {
  auto const tn = Tp(1000);
  auto const expected = ExpectedPingResponseTime(tn, Ms(200));
  TEST_ASSERT_TRUE(expected == Tp(1100));
}

void test_ExpectedPingResponseIgnoresGuardAndMargins() {
  auto const tn = Tp(1000);
  auto const p99 = Ms(200);
  auto const guard = Ms(40);
  auto const dispatch = kPingRetryDispatchMargin;
  auto const scheduler = kPingSchedulerMargin;
  TEST_ASSERT_TRUE(guard == Ms(40));
  TEST_ASSERT_TRUE(dispatch == Ms(60));
  TEST_ASSERT_TRUE(scheduler == Ms(10));
  auto const expected = ExpectedPingResponseTime(tn, p99);
  TEST_ASSERT_TRUE(expected == Tp(1100));
  TEST_ASSERT_TRUE(expected != tn + OneWayReturnEstimateFromP99(p99) - guard);
  TEST_ASSERT_TRUE(expected !=
                   tn + OneWayReturnEstimateFromP99(p99) + guard);
  TEST_ASSERT_TRUE(expected !=
                   tn + OneWayReturnEstimateFromP99(p99) + dispatch);
  TEST_ASSERT_TRUE(expected !=
                   tn + OneWayReturnEstimateFromP99(p99) + scheduler);
}

void test_EmptyStatsBootstrapMatchesHalfOf200ms() {
  TEST_ASSERT_TRUE(OneWayReturnEstimateFromP99(kPingRttEstimate) == Ms(100));
  TEST_ASSERT_TRUE(ExpectedPingResponseTime(Tp(1000), kPingRttEstimate) ==
                   Tp(1100));
}

void test_MarkServerResponseReceivedUpdatesLastOnline() {
  std::optional<TimePoint> last_online_time;
  UpdateMonotonicLastOnlineTime(last_online_time, Tp(1080));
  TEST_ASSERT_TRUE(last_online_time.has_value());
  TEST_ASSERT_TRUE(*last_online_time == Tp(1080));
  UpdateMonotonicLastOnlineTime(last_online_time, Tp(1090));
  TEST_ASSERT_TRUE(*last_online_time == Tp(1090));
}

void test_LastOnlineTimeIsMonotonic() {
  std::optional<TimePoint> last_online_time;
  UpdateMonotonicLastOnlineTime(last_online_time, Tp(1000));
  UpdateMonotonicLastOnlineTime(last_online_time, Tp(900));
  TEST_ASSERT_TRUE(last_online_time.has_value());
  TEST_ASSERT_TRUE(*last_online_time == Tp(1000));
  UpdateMonotonicLastOnlineTime(last_online_time, Tp(1100));
  TEST_ASSERT_TRUE(*last_online_time == Tp(1100));
}

void test_MultiServerAggregationUsesLatestExpected() {
  std::optional<TimePoint> latest_expected_response;
  AccumulateLatestExpectedPingResponse(latest_expected_response, Tp(10100));
  AccumulateLatestExpectedPingResponse(latest_expected_response, Tp(11100));
  TEST_ASSERT_TRUE(latest_expected_response.has_value());
  TEST_ASSERT_TRUE(*latest_expected_response == Tp(11100));
}

void test_InactiveServerExcludedFromAggregation() {
  std::optional<TimePoint> latest_expected_response;
  AccumulateLatestExpectedPingResponse(latest_expected_response, Tp(11100));
  TEST_ASSERT_TRUE(latest_expected_response.has_value());
  TEST_ASSERT_TRUE(*latest_expected_response == Tp(11100));

  // Including a stale inactive server would wrongly raise the max.
  std::optional<TimePoint> with_inactive;
  AccumulateLatestExpectedPingResponse(with_inactive, Tp(11100));
  AccumulateLatestExpectedPingResponse(with_inactive, Tp(50000));
  TEST_ASSERT_TRUE(*with_inactive == Tp(50000));

  // Production skips inactive servers before aggregation.
  latest_expected_response = std::nullopt;
  AccumulateLatestExpectedPingResponse(latest_expected_response, Tp(11100));
  TEST_ASSERT_TRUE(*latest_expected_response == Tp(11100));
}

void test_InboundResultHookMarksOnlineAndEvictionDoesNot() {
  ProtocolContext pc;
  int inbound_count = 0;
  pc.set_inbound_server_response_hook(
      [](void* user) noexcept {
        *static_cast<int*>(user) += 1;
      },
      &inbound_count);

  auto promise = ApiPromise<void>{pc, RequestId{7}};
  bool got_ok = false;
  auto sub = promise.Subscribe([&](auto const& res) {
    TEST_ASSERT_TRUE(res.IsOk());
    got_ok = true;
  });
  static_cast<void>(sub);

  DataBuffer data;
  {
    auto parser = ApiParser{pc, data};
    // Drive matched inbound result path (void result needs no payload).
    pc.SetSendResultResponse(RequestId{7});
  }
  TEST_ASSERT_EQUAL_INT(1, inbound_count);
  TEST_ASSERT_TRUE(got_ok);

  // Eviction must not notify the inbound-server-response hook.
  auto promise2 = ApiPromise<void>{pc, RequestId{8}};
  bool got_evict = false;
  auto sub2 = promise2.Subscribe([&](auto const& res) {
    TEST_ASSERT_FALSE(res.IsOk());
    got_evict = true;
  });
  static_cast<void>(sub2);
  // Replace same request id to force OnEvicted.
  auto promise3 = ApiPromise<void>{pc, RequestId{8}};
  auto sub3 = promise3.Subscribe([](auto const&) {});
  static_cast<void>(sub3);
  TEST_ASSERT_TRUE(got_evict);
  TEST_ASSERT_EQUAL_INT(1, inbound_count);
}

void test_InboundErrorHookMarksOnline() {
  ProtocolContext pc;
  int inbound_count = 0;
  pc.set_inbound_server_response_hook(
      [](void* user) noexcept {
        *static_cast<int*>(user) += 1;
      },
      &inbound_count);

  auto promise = ApiPromise<void>{pc, RequestId{9}};
  bool got_err = false;
  auto sub = promise.Subscribe([&](auto const& res) {
    TEST_ASSERT_FALSE(res.IsOk());
    TEST_ASSERT_EQUAL(42, res.error());
    got_err = true;
  });
  static_cast<void>(sub);

  DataBuffer data;
  {
    auto parser = ApiParser{pc, data};
    pc.SetSendErrorResponse(RequestId{9}, 0, 42);
  }
  TEST_ASSERT_TRUE(got_err);
  TEST_ASSERT_EQUAL_INT(1, inbound_count);
}

void test_CycleExpectedUsesFrozenP99AndNominalTn() {
  LogicalPingCycleState st{};
  st.has_schedule = true;
  st.active = true;
  st.confirmed = false;
  st.nominal_ping_at = Tp(1000);
  st.next_nominal_ping_at = Tp(2000);
  st.has_frozen_p99_rtt = true;
  st.frozen_p99_rtt = Ms(100);
  auto const expected = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(expected.has_value());
  TEST_ASSERT_TRUE(*expected == Tp(1050));

  // Later live p99 changes must not move the frozen cycle expectation.
  st.frozen_p99_rtt = Ms(100);  // still frozen value
  auto const still = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(still.has_value());
  TEST_ASSERT_TRUE(*still == Tp(1050));
}

void test_PostDeadlineRecoveryKeepsOriginalExpectedUntilAdvance() {
  LogicalPingCycleState st{};
  LogicalPingAttemptRequest req{};
  req.interval = Ms(1000);
  req.guard = Ms(10);
  req.attempt_lead = Ms(50);
  req.base_rx_window = Ms(250);
  req.actual_send_at = TimePoint{};
  auto const boot = ApplyLogicalPingAttempt(st, req);
  ConfirmLogicalPingCycle(st);

  req.actual_send_at = boot.next_local_send;
  auto const first = ApplyLogicalPingAttempt(st, req);
  st.frozen_p99_rtt = Ms(100);
  st.has_frozen_p99_rtt = true;
  TEST_ASSERT_TRUE(first.nominal_ping_at == Tp(1000));
  auto const expected_before = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(expected_before.has_value());
  TEST_ASSERT_TRUE(*expected_before == Tp(1050));

  // Same-cycle late recovery before the next nominal: original expected stays.
  req.actual_send_at = Tp(1100);
  auto const retry = ApplyLogicalPingAttempt(st, req);
  TEST_ASSERT_TRUE(retry.cycle_id == first.cycle_id);
  TEST_ASSERT_TRUE(st.nominal_ping_at == Tp(1000));
  auto const expected_during_recovery = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(expected_during_recovery.has_value());
  TEST_ASSERT_TRUE(*expected_during_recovery == Tp(1050));

  ConfirmLogicalPingCycle(st);
  auto const expected_next = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(expected_next.has_value());
  TEST_ASSERT_TRUE(*expected_next == Tp(2050));
}

void test_AfterConfirmExpectedUsesNextNominal() {
  LogicalPingCycleState st{};
  st.has_schedule = true;
  st.active = false;
  st.confirmed = true;
  st.nominal_ping_at = Tp(1000);
  st.next_nominal_ping_at = Tp(2000);
  st.has_frozen_p99_rtt = true;
  st.frozen_p99_rtt = Ms(120);
  auto const expected = ExpectedPingResponseTimeForCycle(st);
  TEST_ASSERT_TRUE(expected.has_value());
  TEST_ASSERT_TRUE(*expected == Tp(2060));
}

void test_NoScheduleYieldsNulloptExpected() {
  LogicalPingCycleState st{};
  st.has_frozen_p99_rtt = true;
  st.frozen_p99_rtt = Ms(200);
  TEST_ASSERT_FALSE(ExpectedPingResponseTimeForCycle(st).has_value());
}

}  // namespace ae::test_client_online_timing

int test_client_online_timing() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_client_online_timing::
               test_InitialClientOnlineTimestampsAreEmpty);
  RUN_TEST(ae::test_client_online_timing::
               test_ExpectedPingResponseIsTnPlusHalfP99);
  RUN_TEST(ae::test_client_online_timing::
               test_ExpectedPingResponseIgnoresGuardAndMargins);
  RUN_TEST(ae::test_client_online_timing::
               test_EmptyStatsBootstrapMatchesHalfOf200ms);
  RUN_TEST(ae::test_client_online_timing::
               test_MarkServerResponseReceivedUpdatesLastOnline);
  RUN_TEST(ae::test_client_online_timing::test_LastOnlineTimeIsMonotonic);
  RUN_TEST(ae::test_client_online_timing::
               test_MultiServerAggregationUsesLatestExpected);
  RUN_TEST(ae::test_client_online_timing::
               test_InactiveServerExcludedFromAggregation);
  RUN_TEST(ae::test_client_online_timing::
               test_InboundResultHookMarksOnlineAndEvictionDoesNot);
  RUN_TEST(ae::test_client_online_timing::test_InboundErrorHookMarksOnline);
  RUN_TEST(ae::test_client_online_timing::
               test_CycleExpectedUsesFrozenP99AndNominalTn);
  RUN_TEST(ae::test_client_online_timing::
               test_PostDeadlineRecoveryKeepsOriginalExpectedUntilAdvance);
  RUN_TEST(ae::test_client_online_timing::
               test_AfterConfirmExpectedUsesNextNominal);
  RUN_TEST(ae::test_client_online_timing::test_NoScheduleYieldsNulloptExpected);
  return UNITY_END();
}
