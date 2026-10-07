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
#include <cstdio>
#include <vector>

#include <unity.h>

#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "ae-numeric/percentile.h"
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
      ComputeCloudRequestSoftTimeout(p95, CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(95.0), TimeoutFactor8::FromDouble(1.0), 1, 0));
  auto const t95_12 =
      ComputeCloudRequestSoftTimeout(p95, CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(95.0), TimeoutFactor8::FromDouble(1.2), 1, 0));
  auto const t99_10 =
      ComputeCloudRequestSoftTimeout(p99, CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.0), 1, 0));
  auto const t99_12 =
      ComputeCloudRequestSoftTimeout(p99, CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 1, 0));
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(t95_10).count());
  // 500ms * TimeoutFactor8(1.2) raw=77 / 64 = 601.5625 → 602 nearest
  TEST_ASSERT_EQUAL(602, std::chrono::duration_cast<Ms>(t95_12).count());
  TEST_ASSERT_EQUAL(500, std::chrono::duration_cast<Ms>(t99_10).count());
  TEST_ASSERT_EQUAL(602, std::chrono::duration_cast<Ms>(t99_12).count());

  // Rounding with quantized factor 77/64: 100*77/64=120.3125→120; 101*77/64=121.515625→122
  TEST_ASSERT_EQUAL(
      120, std::chrono::duration_cast<Ms>(
               ScaleDurationByTimeoutFactor(Duration{Ms{100}}, TimeoutFactor8::FromDouble(1.2)))
               .count());
  TEST_ASSERT_EQUAL(
      122, std::chrono::duration_cast<Ms>(
               ScaleDurationByTimeoutFactor(Duration{Ms{101}}, TimeoutFactor8::FromDouble(1.2)))
               .count());
}

void test_RetryCountSemantics() {
  CloudRequestExecutionPolicy p0 =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 0, 0);
  TEST_ASSERT_EQUAL(1, p0.TotalAttempts());
  CloudRequestServerExecState s0;
  s0.activated = true;
  TEST_ASSERT_EQUAL(1, s0.StartAttempt(p0));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(s0.OnSoftTimeout(p0)));
  TEST_ASSERT_TRUE(s0.exhausted);

  CloudRequestExecutionPolicy p1 =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 1, 0);
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
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
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
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 2);
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
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
  CloudRequestServerExecState s1;
  s1.activated = true;
  s1.StartAttempt(policy);
  s1.OnSoftTimeout(policy);
  TEST_ASSERT_EQUAL(0, s1.HedgeCountOnThisMiss(policy));
}

void test_LateResponseAfterSoftTimeout() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
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
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 1, 0));
  auto const t2 = ComputeCloudRequestSoftTimeout(
      Duration{Ms{300}},
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 1, 0));
  TEST_ASSERT_EQUAL(120, std::chrono::duration_cast<Ms>(t1).count());
  // 300ms * 77/64 = 360.9375 → 361 nearest
  TEST_ASSERT_EQUAL(361, std::chrono::duration_cast<Ms>(t2).count());
}

void test_PolicySnapshotDefaults() {
  auto const d = CloudRequestExecutionPolicy::Default();
  TEST_ASSERT_EQUAL_UINT16(
      Percentile::FromPercent(99.0).TailPercent().RawValue(),
      d.response_percentile.TailPercent().RawValue());
  TEST_ASSERT_EQUAL_UINT8(TimeoutFactor8::FromDouble(1.2).RawValue(), d.timeout_factor.RawValue());
  TEST_ASSERT_EQUAL(1, d.retry_count);
  TEST_ASSERT_EQUAL(0, d.hedge_next_servers);
  TEST_ASSERT_EQUAL(2, d.TotalAttempts());
  static_assert(sizeof(Percentile) == 2);
  static_assert(sizeof(TimeoutFactor8) == 1);
  std::printf("sizeof(CloudRequestExecutionPolicy)=%zu\n",
              sizeof(CloudRequestExecutionPolicy));
}

void test_PercentileFractionalDistinctRanks() {
  // Need N large enough that quantized p99.9 / p99.99 ranks differ
  // (see PercentileIndex: diverge by N≈10000).
  StatisticsCounter<int, 10000> stats;
  for (int i = 0; i < 10000; ++i) {
    stats.Add(i);
  }
  auto const p99 = stats.PercentileValue(Percentile::FromPercent(99.0));
  auto const p999 = stats.PercentileValue(Percentile::FromPercent(99.9));
  auto const p9999 = stats.PercentileValue(Percentile::FromPercent(99.99));
  std::printf(
      "selected RTT ranks (samples 0..9999): p99=%d p99.9=%d p99.99=%d\n", p99,
      p999, p9999);
  TEST_ASSERT_TRUE(p99 <= p999);
  TEST_ASSERT_TRUE(p999 <= p9999);
  TEST_ASSERT_TRUE(p999 < p9999);
  TEST_ASSERT_TRUE(PercentileIndex(1'000'000, Percentile::FromPercent(99.9)) <
                   PercentileIndex(1'000'000, Percentile::FromPercent(99.99)));
  TEST_ASSERT_EQUAL(PercentileIndexInteger(1000, 95),
                    PercentileIndex(1000, Percentile::FromPercent(95.0)));
}

void test_PolicyFieldsAreRuntimeAssignable() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(95.0),
                                              TimeoutFactor8::FromDouble(1.0),
                                              1, 0);
  auto const snap = policy;
  policy.response_percentile = Percentile::FromPercent(99.99);
  policy.timeout_factor = TimeoutFactor8::FromDouble(1.2);
  TEST_ASSERT_EQUAL_UINT16(
      Percentile::FromPercent(95.0).TailPercent().RawValue(),
      snap.response_percentile.TailPercent().RawValue());
  TEST_ASSERT_EQUAL_UINT8(TimeoutFactor8::FromDouble(1.0).RawValue(),
                          snap.timeout_factor.RawValue());
  TEST_ASSERT_EQUAL_UINT16(
      Percentile::FromPercent(99.99).TailPercent().RawValue(),
      policy.response_percentile.TailPercent().RawValue());
}

void test_RetryCountClampAndMax() {
  TEST_ASSERT_EQUAL(31, kMaxCloudRequestRetryCount);

  CloudRequestExecutionPolicy p0 =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 0, 0);
  TEST_ASSERT_EQUAL(0, p0.retry_count);
  TEST_ASSERT_EQUAL(1, p0.TotalAttempts());

  CloudRequestExecutionPolicy p1 =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 1, 0);
  TEST_ASSERT_EQUAL(1, p1.retry_count);
  TEST_ASSERT_EQUAL(2, p1.TotalAttempts());

  CloudRequestExecutionPolicy p31 =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 31, 0);
  TEST_ASSERT_EQUAL(31, p31.retry_count);
  TEST_ASSERT_EQUAL(32, p31.TotalAttempts());

  CloudRequestExecutionPolicy over{};
  over.retry_count = 255;
  NormalizeCloudRequestExecutionPolicy(over);
  TEST_ASSERT_EQUAL(31, over.retry_count);
  TEST_ASSERT_EQUAL(32, over.TotalAttempts());

  auto const from_over =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 255, 0);
  TEST_ASSERT_EQUAL(31, from_over.retry_count);
  TEST_ASSERT_EQUAL(32, from_over.TotalAttempts());
}

void test_RetryCount31StateMachine() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 31, 0);
  TEST_ASSERT_EQUAL(32, policy.TotalAttempts());

  CloudRequestServerExecState s;
  s.activated = true;
  for (int i = 0; i < 31; ++i) {
    TEST_ASSERT_TRUE(s.CanStartAttempt(policy));
    TEST_ASSERT_EQUAL(i + 1, s.StartAttempt(policy));
    auto const action = s.OnSoftTimeout(policy);
    TEST_ASSERT_EQUAL(
        static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
        static_cast<int>(action));
    TEST_ASSERT_FALSE(s.exhausted);
  }
  TEST_ASSERT_EQUAL(31, s.attempts_started);
  TEST_ASSERT_EQUAL(31, s.soft_timeouts);
  TEST_ASSERT_EQUAL(32, s.StartAttempt(policy));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kExhaust),
      static_cast<int>(s.OnSoftTimeout(policy)));
  TEST_ASSERT_TRUE(s.exhausted);
  TEST_ASSERT_EQUAL(32, s.attempts_started);
  TEST_ASSERT_EQUAL(32, s.soft_timeouts);
  TEST_ASSERT_EQUAL(0, s.StartAttempt(policy));  // no uint8 wrap / extra
}

void test_ChannelChangedOneCallbackPerServer() {
  // retry_count=2: after attempt #1 soft timeout and attempt #2 started,
  // one channel-changed event must produce exactly one OnChannelChanged
  // decision and at most one additional attempt.
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
  CloudRequestServerExecState s;
  s.activated = true;
  TEST_ASSERT_EQUAL(1, s.StartAttempt(policy));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kRetry),
      static_cast<int>(s.OnSoftTimeout(policy)));
  TEST_ASSERT_EQUAL(2, s.StartAttempt(policy));
  // Simulate two outstanding attempts (#1 timed out late-response possible,
  // #2 active) — still one channel-changed subscription / one callback.
  auto const a = s.OnChannelChanged(policy);
  TEST_ASSERT_EQUAL(1, s.channel_changed_events);
  TEST_ASSERT_EQUAL(
      static_cast<int>(
          CloudRequestServerExecState::ChannelChangedAction::kRetry),
      static_cast<int>(a));
  TEST_ASSERT_EQUAL(3, s.StartAttempt(policy));
  TEST_ASSERT_FALSE(s.exhausted);
  // Budget exhausted: further channel change must not start more attempts.
  auto const b = s.OnChannelChanged(policy);
  TEST_ASSERT_EQUAL(2, s.channel_changed_events);
  TEST_ASSERT_EQUAL(
      static_cast<int>(
          CloudRequestServerExecState::ChannelChangedAction::kExhaust),
      static_cast<int>(b));
  TEST_ASSERT_TRUE(s.exhausted);
  TEST_ASSERT_EQUAL(3, s.attempts_started);
}

void test_ChannelChangedThreeOutstandingAttempts() {
  // Three outstanding attempts (retry_count=2, all started via soft path /
  // channel), then one channel event must still be a single decision.
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
  CloudRequestServerExecState s;
  s.activated = true;
  s.StartAttempt(policy);  // #1
  s.OnSoftTimeout(policy);
  s.StartAttempt(policy);  // #2
  s.OnSoftTimeout(policy);
  s.StartAttempt(policy);  // #3 — budget full, three outstanding conceptually
  TEST_ASSERT_EQUAL(3, s.attempts_started);
  TEST_ASSERT_FALSE(s.CanStartAttempt(policy));

  auto const a = s.OnChannelChanged(policy);
  TEST_ASSERT_EQUAL(1, s.channel_changed_events);
  TEST_ASSERT_EQUAL(
      static_cast<int>(
          CloudRequestServerExecState::ChannelChangedAction::kExhaust),
      static_cast<int>(a));
  TEST_ASSERT_TRUE(s.exhausted);
  TEST_ASSERT_EQUAL(3, s.attempts_started);  // no extra launch
  TEST_ASSERT_EQUAL(0, s.StartAttempt(policy));
}

void test_ApiErrorDoesNotQuarantine() {
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
  CloudRequestServerExecState s;
  s.activated = true;
  TEST_ASSERT_EQUAL(1, s.StartAttempt(policy));
  s.MarkRemoteErrorCompleted();
  TEST_ASSERT_TRUE(s.remote_error_completed);
  TEST_ASSERT_TRUE(s.IsTerminal());
  TEST_ASSERT_FALSE(s.exhausted);
  TEST_ASSERT_FALSE(s.succeeded);
  TEST_ASSERT_EQUAL(0, s.soft_timeouts);
  TEST_ASSERT_FALSE(s.CanStartAttempt(policy));
  TEST_ASSERT_EQUAL(
      static_cast<int>(CloudRequestServerExecState::SoftTimeoutAction::kIgnore),
      static_cast<int>(s.OnSoftTimeout(policy)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(
          CloudRequestServerExecState::ChannelChangedAction::kIgnore),
      static_cast<int>(s.OnChannelChanged(policy)));
  TEST_ASSERT_EQUAL(0, s.channel_changed_events);
  TEST_ASSERT_EQUAL(0, s.soft_timeouts);
  TEST_ASSERT_FALSE(s.exhausted);  // no no-response quarantine path
}

void test_NoResponseStillQuarantinesAfterBudget() {
  // retry_count=2 => attempts=3 soft timeouts then exhaust (=quarantine point).
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 0);
  CloudRequestServerExecState s;
  s.activated = true;
  s.StartAttempt(policy);
  s.OnSoftTimeout(policy);
  TEST_ASSERT_FALSE(s.exhausted);
  s.StartAttempt(policy);
  s.OnSoftTimeout(policy);
  TEST_ASSERT_FALSE(s.exhausted);
  s.StartAttempt(policy);
  s.OnSoftTimeout(policy);
  TEST_ASSERT_TRUE(s.exhausted);
  TEST_ASSERT_EQUAL(3, s.attempts_started);
  TEST_ASSERT_EQUAL(3, s.soft_timeouts);
}

void test_DeterministicLatencyTimeline() {
  // p99=100ms, factor=1.2 => T=120ms per attempt when RTT fixed.
  CloudRequestExecutionPolicy policy =
      CloudRequestExecutionPolicy::FromFactor(Percentile::FromPercent(99.0), TimeoutFactor8::FromDouble(1.2), 2, 1);
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
  RUN_TEST(ae::test_cloud_request::test_RetryCountClampAndMax);
  RUN_TEST(ae::test_cloud_request::test_RetryCount31StateMachine);
  RUN_TEST(ae::test_cloud_request::test_NoQuarantineBeforeExhaustionAndHedge);
  RUN_TEST(ae::test_cloud_request::test_HedgeZeroKeepsSequential);
  RUN_TEST(ae::test_cloud_request::test_LateResponseAfterSoftTimeout);
  RUN_TEST(ae::test_cloud_request::test_ChannelChangedOneCallbackPerServer);
  RUN_TEST(ae::test_cloud_request::test_ChannelChangedThreeOutstandingAttempts);
  RUN_TEST(ae::test_cloud_request::test_ApiErrorDoesNotQuarantine);
  RUN_TEST(ae::test_cloud_request::test_NoResponseStillQuarantinesAfterBudget);
  RUN_TEST(ae::test_cloud_request::test_PerServerTimeoutIndependent);
  RUN_TEST(ae::test_cloud_request::test_PolicySnapshotDefaults);
  RUN_TEST(ae::test_cloud_request::test_PercentileFractionalDistinctRanks);
  RUN_TEST(ae::test_cloud_request::test_PolicyFieldsAreRuntimeAssignable);
  RUN_TEST(ae::test_cloud_request::test_DeterministicLatencyTimeline);
  return UNITY_END();
}
