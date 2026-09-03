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

#include <chrono>
#include <cstdint>
#include <vector>

#include <unity.h>

#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/types/statistic_counter.h"

namespace ae::test_cloud_request {

using Ms = std::chrono::milliseconds;

void test_TimeoutCalculation() {
  // Deterministic RTT samples: 50,100,150,200,250,300,350,400,450,500
  StatisticsCounter<Duration, 16> stats;
  for (int i = 1; i <= 10; ++i) {
    stats.Add(Duration{Ms{50 * i}});
  }
  auto const p95 = stats.PercentileValue(95);
  auto const p99 = stats.PercentileValue(99);
  // index = ceil((10-1)*pct/100): p95 -> ceil(8.55)=9 -> 500? wait
  // sorted 50..500, index ceil(9*0.95)=ceil(8.55)=9 -> value_buffer[9]=500
  // p99: ceil(9*0.99)=ceil(8.91)=9 -> 500
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(p95).count());
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(p99).count());

  auto const t95_10 =
      ComputeCloudRequestSoftTimeout(p95, CloudRequestExecutionPolicy::FromFactor(
                                              95, 1.0, 1, 0));
  auto const t95_12 =
      ComputeCloudRequestSoftTimeout(p95, CloudRequestExecutionPolicy::FromFactor(
                                              95, 1.2, 1, 0));
  auto const t99_10 =
      ComputeCloudRequestSoftTimeout(p99, CloudRequestExecutionPolicy::FromFactor(
                                              99, 1.0, 1, 0));
  auto const t99_12 =
      ComputeCloudRequestSoftTimeout(p99, CloudRequestExecutionPolicy::FromFactor(
                                              99, 1.2, 1, 0));
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(t95_10).count());
  TEST_ASSERT_EQUAL(600, std::chrono::duration_cast<Ms>(t95_12).count());
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(t99_10).count());
  TEST_ASSERT_EQUAL(600, std::chrono::duration_cast<Ms>(t99_12).count());

  // Rounding: 100ms * 1.2 = 120 exactly; 101 * 1.2 = 121.2 -> 121
  TEST_ASSERT_EQUAL(
      120, std::chrono::duration_cast<Ms>(
               ScaleDurationByPermille(Duration{Ms{100}}, 1200))
               .count());
  TEST_ASSERT_EQUAL(
      121, std::chrono::duration_cast<Ms>(
               ScaleDurationByPermille(Duration{Ms{101}}, 1200))
               .count());
}

void test_RetryCountSemantics() {
  CloudRequestExecutionPolicy p0 =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 0, 0);
  TEST_ASSERT_EQUAL(1, p0.TotalAttempts());
  CloudRequestServerExecState s0;
  s0.activated = true;
  TEST_ASSERT_EQUAL(1, s0.StartAttempt(p0));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(s0.OnSoftTimeout(p0)));
  TEST_ASSERT_TRUE(s0.exhausted);

  CloudRequestExecutionPolicy p1 =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 1, 0);
  TEST_ASSERT_EQUAL(2, p1.TotalAttempts());
  CloudRequestServerExecState s1;
  s1.activated = true;
  TEST_ASSERT_EQUAL(1, s1.StartAttempt(p1));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(s1.OnSoftTimeout(p1)));
  TEST_ASSERT_FALSE(s1.exhausted);
  TEST_ASSERT_EQUAL(2, s1.StartAttempt(p1));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(s1.OnSoftTimeout(p1)));
  TEST_ASSERT_TRUE(s1.exhausted);

  CloudRequestExecutionPolicy p2 =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 2, 0);
  TEST_ASSERT_EQUAL(3, p2.TotalAttempts());
  CloudRequestServerExecState s2;
  s2.activated = true;
  TEST_ASSERT_EQUAL(1, s2.StartAttempt(p2));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(s2.OnSoftTimeout(p2)));
  TEST_ASSERT_EQUAL(2, s2.StartAttempt(p2));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(s2.OnSoftTimeout(p2)));
  TEST_ASSERT_EQUAL(3, s2.StartAttempt(p2));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(s2.OnSoftTimeout(p2)));
  TEST_ASSERT_EQUAL(3, s2.attempts_started);
  TEST_ASSERT_EQUAL(3, s2.soft_timeouts);
}

void test_NoQuarantineBeforeExhaustionAndHedge() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 2, 2);
  CloudRequestServerExecState s;
  s.activated = true;
  TEST_ASSERT_EQUAL(1, s.StartAttempt(policy));
  auto const a1 = s.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(a1));
  TEST_ASSERT_FALSE(s.exhausted);
  TEST_ASSERT_EQUAL(2, s.HedgeCountOnThisMiss(policy));

  TEST_ASSERT_EQUAL(2, s.StartAttempt(policy));
  auto const a2 = s.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(a2));
  TEST_ASSERT_EQUAL(0, s.HedgeCountOnThisMiss(policy));  // only first miss
  TEST_ASSERT_FALSE(s.exhausted);

  TEST_ASSERT_EQUAL(3, s.StartAttempt(policy));
  auto const a3 = s.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(a3));
  TEST_ASSERT_TRUE(s.exhausted);
}

void test_HedgeZeroKeepsSequential() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 2, 0);
  CloudRequestServerExecState s1;
  s1.activated = true;
  s1.StartAttempt(policy);
  s1.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(0, s1.HedgeCountOnThisMiss(policy));
}

void test_LateResponseAfterSoftTimeout() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 2, 0);
  CloudRequestServerExecState s;
  s.activated = true;
  TEST_ASSERT_EQUAL(1, s.StartAttempt(policy));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(s.OnSoftTimeout(policy)));
  TEST_ASSERT_EQUAL(2, s.StartAttempt(policy));
  // Late success for attempt #1 — mark succeeded, no further attempts / exhaust.
  s.MarkSucceeded();
  TEST_ASSERT_TRUE(s.succeeded);
  TEST_ASSERT_FALSE(s.exhausted);
  TEST_ASSERT_FALSE(s.CanStartAttempt(policy));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kIgnore),
      static_cast<int>(s.OnSoftTimeout(policy)));
}

void test_PerServerTimeoutIndependent() {
  auto const t1 = ComputeCloudRequestSoftTimeout(
      Duration{Ms{100}},
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 1, 0));
  auto const t2 = ComputeCloudRequestSoftTimeout(
      Duration{Ms{300}},
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 1, 0));
  TEST_ASSERT_EQUAL(120, std::chrono::duration_cast<Ms>(t1).count());
  TEST_ASSERT_EQUAL(360, std::chrono::duration_cast<Ms>(t2).count());
}

void test_PolicySnapshotDefaults() {
  auto const d = CloudRequestExecutionPolicy::Default();
  TEST_ASSERT_EQUAL(99, d.response_percentile);
  TEST_ASSERT_EQUAL(1200, d.timeout_factor_permille);
  TEST_ASSERT_EQUAL(1, d.retry_count);
  TEST_ASSERT_EQUAL(0, d.hedge_next_servers);
  TEST_ASSERT_EQUAL(2, d.TotalAttempts());
}

void test_DeterministicLatencyTimeline() {
  // p99=100ms, factor=1.2 => T=120ms per attempt when RTT fixed.
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, 2, 1);
  auto const T =
      ComputeCloudRequestSoftTimeout(Duration{Ms{100}}, policy);
  TEST_ASSERT_EQUAL(120, std::chrono::duration_cast<Ms>(T).count());

  // Case A: response at 110 < 120 => no soft miss conceptually (timer cancelled).
  // Case B: soft miss at 120 => retry + hedge; late at 130 accepted.
  CloudRequestServerExecState s;
  s.activated = true;
  s.StartAttempt(policy);
  auto const miss = s.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(miss));
  TEST_ASSERT_EQUAL(1, s.HedgeCountOnThisMiss(policy));
  s.StartAttempt(policy);
  s.MarkSucceeded();  // late response from attempt #1
  TEST_ASSERT_TRUE(s.succeeded);
  TEST_ASSERT_FALSE(s.exhausted);

  // Case C: no responses, retry_count=2 => three timeouts then exhaust.
  CloudRequestServerExecState never;
  never.activated = true;
  std::int64_t t_ms = 0;
  never.StartAttempt(policy);
  t_ms += 120;
  never.OnSoftTimeout(policy);
  never.StartAttempt(policy);
  t_ms += 120;
  never.OnSoftTimeout(policy);
  never.StartAttempt(policy);
  t_ms += 120;
  never.OnSoftTimeout(policy);
  TEST_ASSERT_TRUE(never.exhausted);
  TEST_ASSERT_EQUAL(360, t_ms);
  TEST_ASSERT_EQUAL(3, never.attempts_started);
}

}  // namespace ae::test_cloud_request

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_cloud_request::test_TimeoutCalculation);
  RUN_TEST(ae::test_cloud_request::test_RetryCountSemantics);
  RUN_TEST(ae::test_cloud_request::test_NoQuarantineBeforeExhaustionAndHedge);
  RUN_TEST(ae::test_cloud_request::test_HedgeZeroKeepsSequential);
  RUN_TEST(ae::test_cloud_request::test_LateResponseAfterSoftTimeout);
  RUN_TEST(ae::test_cloud_request::test_PerServerTimeoutIndependent);
  RUN_TEST(ae::test_cloud_request::test_PolicySnapshotDefaults);
  RUN_TEST(ae::test_cloud_request::test_DeterministicLatencyTimeline);
  return UNITY_END();
}
