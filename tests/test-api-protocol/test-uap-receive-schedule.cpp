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
#include <type_traits>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/channels/channel.h"
#include "aether/cloud_connections/ping_schedule_guard.h"
#include "aether/receive_schedule.h"
#include "aether/types/statistic_counter.h"
#include "aether/work_cloud_api/client_timing.h"
#include "aether/work_cloud_api/uap.h"

#include "examples/benches/aether_uap_delivery_timing_bench/common/bench_message.h"

namespace ae::test_uap_receive_schedule {
namespace {

Duration Ms(std::uint32_t v) {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{v});
}

}  // namespace

void test_ResponseTimeoutEmptyUsesInitialEstimate() {
  StatisticsCounter<Duration, 8> stats{};
  TEST_ASSERT_TRUE(stats.empty());
  // Channel::ResponseTimeout() returns kInitialResponseEstimate when empty.
  auto const timeout = stats.empty() ? Channel::kInitialResponseEstimate
                                     : stats.percentile<99>();
  TEST_ASSERT_TRUE(timeout == Channel::kInitialResponseEstimate);
  TEST_ASSERT_TRUE(timeout == Ms(200));
}

void test_ResponseTimeoutAfterFirstSampleUsesPercentile() {
  StatisticsCounter<Duration, 8> stats{};
  stats.Add(Ms(50));
  TEST_ASSERT_FALSE(stats.empty());
  TEST_ASSERT_EQUAL_UINT(1, stats.size());
  TEST_ASSERT_TRUE(stats.min() == stats.percentile<99>());
  TEST_ASSERT_TRUE(stats.percentile<99>() == Ms(50));
}

void test_NoSyntheticSeedMeansEmptyUntilRealRtt() {
  StatisticsCounter<Duration, 8> stats{};
  TEST_ASSERT_TRUE(stats.empty());
  stats.Add(Ms(12));
  TEST_ASSERT_EQUAL_UINT(1, stats.size());
}

void test_PingDeadlineGuardFormulas() {
  StatisticsCounter<Duration, 8> empty{};
  TEST_ASSERT_TRUE(ComputePingSendGuardFromStats(empty, Ms(3000)) == Ms(10));
  StatisticsCounter<Duration, 8> one{};
  one.Add(Ms(80));
  TEST_ASSERT_TRUE(ComputePingSendGuardFromStats(one, Ms(3000)) == Ms(10));
  TEST_ASSERT_TRUE(ComputePingSendGuard(Ms(60), Ms(100)) == Ms(30));
  TEST_ASSERT_TRUE(ClampPingSendGuard(Ms(200), Ms(30)) == Ms(29));
}

void test_WireAnnouncedIntervalUnchangedVsLocalSendEarlier() {
  auto const interval = Ms(3000);
  auto const guard = ComputePingSendGuard(Ms(60), Ms(100));
  TEST_ASSERT_TRUE(guard == Ms(30));
  TEST_ASSERT_TRUE((interval - guard) == Ms(2970));
}

void test_RxCloseIsPongPlusWindowNotSendPlusWindow() {
  auto const send = TimePoint{} + std::chrono::seconds{1};
  auto const pong = send + std::chrono::milliseconds{35};
  auto const window = Ms(200);
  auto const close_at = ComputeRxWindowCloseTime(pong, window);
  TEST_ASSERT_TRUE(close_at == pong + window);
  TEST_ASSERT_TRUE(close_at != send + window);
}

void test_ConversionUsesLibraryTimePointOnly() {
  auto const qsend = TimePoint{} + std::chrono::milliseconds{1000};
  auto const one_way = OneWayPingEstimate(false, Ms(80));
  TEST_ASSERT_TRUE(one_way == Ms(40));
  ClientTiming const timing{4'000, -1'000};
  auto const converted = ConvertClientTiming(qsend, one_way, timing);
  TEST_ASSERT_TRUE(converted.last_online ==
                   qsend + one_way - std::chrono::milliseconds{1000});
  TEST_ASSERT_TRUE(converted.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*converted.next_ping_deadline ==
                   qsend + one_way + std::chrono::milliseconds{4000});
  TEST_ASSERT_TRUE(TimePointOffsetByMs(qsend, -1'000) ==
                   qsend - std::chrono::milliseconds{1000});
  static_assert(std::is_same_v<decltype(converted.last_online), TimePoint>);
}

void test_DeltaZeroYieldsUnknownDeadline() {
  auto const converted = ConvertClientTiming(
      TimePoint{} + std::chrono::seconds{5}, Ms(40), ClientTiming{0, -100});
  TEST_ASSERT_FALSE(converted.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(converted.state == PeerScheduleState::kUnknown);
}

void test_NegativeDeltaYieldsMissedDeadline() {
  auto const converted =
      ConvertClientTiming(TimePoint{}, Ms(40), ClientTiming{-1, -10});
  TEST_ASSERT_TRUE(converted.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(converted.state == PeerScheduleState::kMissedDeadline);
}

void test_UapWireFieldOrderAndSignedInt64() {
  Uap const uap{5'500, 1'700'000'000'000};
  std::vector<std::uint8_t> packed;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Save(uap);
  }
  TEST_ASSERT_EQUAL_UINT(16, packed.size());
  std::int64_t delta = 0;
  std::int64_t last_read = 0;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Load(delta);
    archive.Load(last_read);
  }
  TEST_ASSERT_EQUAL_INT64(5'500, delta);
  TEST_ASSERT_EQUAL_INT64(1'700'000'000'000, last_read);
}

void test_BenchMessageCrcRoundTrip() {
  ae::bench::uap::DeliveryBenchMessage msg{};
  msg.offset_ms = 250;
  msg.sequence = 42;
  msg.send_qpc = 0x1122334455667788ull;
  auto const bytes = ae::bench::uap::SerializeDeliveryBenchMessage(msg);
  auto decoded = ae::bench::uap::DeserializeDeliveryBenchMessage(bytes.data(),
                                                                 bytes.size());
  TEST_ASSERT_TRUE(decoded.has_value());
  TEST_ASSERT_EQUAL_UINT16(msg.offset_ms, decoded->offset_ms);
  TEST_ASSERT_EQUAL_UINT32(msg.sequence, decoded->sequence);
  TEST_ASSERT_EQUAL_UINT64(msg.send_qpc, decoded->send_qpc);
  TEST_ASSERT_EQUAL_UINT32(ae::bench::uap::DeliveryBenchMessageCrc(*decoded),
                           decoded->crc);

  auto tampered = bytes;
  tampered[8] ^= 0xFFu;
  auto bad = ae::bench::uap::DeserializeDeliveryBenchMessage(tampered.data(),
                                                             tampered.size());
  TEST_ASSERT_FALSE(bad.has_value());
}

void test_ClassifyReceiveSendOffset() {
  auto const window = Ms(1000);
  TEST_ASSERT_TRUE(ClassifyReceiveSendOffset(Ms(500), window) ==
                   ReceiveSendPhase::kInsideReceiveWindow);
  TEST_ASSERT_TRUE(ClassifyReceiveSendOffset(Ms(1500), window) ==
                   ReceiveSendPhase::kOutsideReceiveWindow);
}

void test_EarlyRxWindowComputation() {
  auto const t0 = TimePoint{};
  auto at = [&](std::uint32_t ms) { return t0 + Ms(ms); };

  auto none = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = false,
      .actual_send_at = at(100),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(none.early_by == Ms(0));
  TEST_ASSERT_TRUE(none.effective_wire_rx_window == Ms(1000));

  auto on_time = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(3000),
      .actual_send_at = at(3000),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(on_time.early_by == Ms(0));
  TEST_ASSERT_TRUE(on_time.effective_wire_rx_window == Ms(1000));

  auto early700 = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(3000),
      .actual_send_at = at(2300),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(early700.early_by == Ms(700));
  TEST_ASSERT_TRUE(early700.effective_wire_rx_window == Ms(1700));

  auto early1200 = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(3000),
      .actual_send_at = at(1800),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(early1200.early_by == Ms(1200));
  TEST_ASSERT_TRUE(early1200.effective_wire_rx_window == Ms(2200));

  auto late = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(3000),
      .actual_send_at = at(3200),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(late.early_by == Ms(0));
  TEST_ASSERT_TRUE(late.effective_wire_rx_window == Ms(1000));

  auto held = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(7000),
      .actual_send_at = at(7000),
      .base_rx_window = Ms(1000),
      .has_required_rx_until = true,
      .required_rx_until = at(10000),
  });
  TEST_ASSERT_TRUE(held.effective_wire_rx_window == Ms(3000));

  auto second = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(2800),
      .actual_send_at = at(2800),
      .base_rx_window = Ms(1000),
      .has_required_rx_until = true,
      .required_rx_until = early1200.required_rx_until,
  });
  TEST_ASSERT_TRUE(second.required_rx_until >= early1200.required_rx_until);
  TEST_ASSERT_TRUE(second.effective_wire_rx_window >=
                   SaturatingSubTime(early1200.required_rx_until, at(2800)));

  auto zero_base = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(3000),
      .actual_send_at = at(1800),
      .base_rx_window = Ms(0),
  });
  TEST_ASSERT_TRUE(zero_base.effective_wire_rx_window == Ms(1200));

  auto past_planned = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = at(100),
      .actual_send_at = at(2000),
      .base_rx_window = Ms(1000),
  });
  TEST_ASSERT_TRUE(past_planned.early_by == Ms(0));
  TEST_ASSERT_TRUE(past_planned.effective_wire_rx_window == Ms(1000));

  auto overflow = SaturatingAddTime(TimePoint::max(), Ms(1000));
  TEST_ASSERT_TRUE(overflow == TimePoint::max());
  TEST_ASSERT_TRUE(DurationToSaturatedInt64Ms(Duration::max()) > 0);
}

void test_LocalRxWindowMonotonicAndStaleTimer() {
  LocalRxWindowState s{};
  auto const t0 = TimePoint{};
  TEST_ASSERT_TRUE(ExtendLocalRxUntil(s, t0 + Ms(5000)));
  TEST_ASSERT_EQUAL_UINT64(1, s.generation);
  TEST_ASSERT_TRUE(ExtendLocalRxUntil(s, t0 + Ms(8000)));
  auto const gen8 = s.generation;
  TEST_ASSERT_FALSE(ExtendLocalRxUntil(s, t0 + Ms(6000)));
  TEST_ASSERT_EQUAL_UINT64(gen8, s.generation);
  TEST_ASSERT_TRUE(s.close_at == t0 + Ms(8000));
  TEST_ASSERT_FALSE(ShouldApplyCloseTimer(s, 1, t0 + Ms(5000)));
  TEST_ASSERT_TRUE(ShouldApplyCloseTimer(s, gen8, t0 + Ms(8000)));
  TEST_ASSERT_FALSE(ShouldCloseLocalRxAfterWriteFailure(
      s, true, t0 + Ms(8000), t0 + Ms(6000)));
  TEST_ASSERT_TRUE(ShouldCloseLocalRxAfterWriteFailure(
      LocalRxWindowState{}, false, TimePoint{}, t0 + Ms(6000)));
  auto const close_pong =
      ComputeRxWindowCloseTime(t0 + Ms(1900), Ms(2200));
  TEST_ASSERT_TRUE(close_pong == t0 + Ms(4100));
  auto const close_timeout =
      ComputeRxWindowCloseTime(t0 + Ms(2500), Ms(2200));
  TEST_ASSERT_TRUE(close_timeout == t0 + Ms(4700));
  TEST_ASSERT_FALSE(ExtendLocalRxUntil(s, close_timeout));
  TEST_ASSERT_TRUE(s.close_at == t0 + Ms(8000));
  TEST_ASSERT_TRUE(ExtendLocalRxUntil(s, t0 + Ms(9000)));
  TEST_ASSERT_TRUE(s.close_at == t0 + Ms(9000));
  CloseLocalRx(s);
  TEST_ASSERT_FALSE(s.open);
  CloseLocalRx(s);
  TEST_ASSERT_FALSE(s.open);
}

void test_ServerWindowInvariantIndependentOfRtt() {
  auto const t0 = TimePoint{};
  auto const planned = t0 + Ms(3000);
  auto const actual = t0 + Ms(1800);
  auto const base = Ms(1000);
  auto const out = ComputeEarlyRxWindow(EarlyRxWindowInput{
      .has_planned_send = true,
      .planned_send_at = planned,
      .actual_send_at = actual,
      .base_rx_window = base,
  });
  auto const business_end = planned + base;
  for (auto delay_ms : {0u, 30u, 50u}) {
    auto const server_receive = actual + Ms(delay_ms);
    auto const server_end =
        SaturatingAddTime(server_receive, out.effective_wire_rx_window);
    TEST_ASSERT_TRUE(server_end >= business_end);
  }
}

}  // namespace ae::test_uap_receive_schedule

int test_uap_receive_schedule() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_uap_receive_schedule::
               test_ResponseTimeoutEmptyUsesInitialEstimate);
  RUN_TEST(ae::test_uap_receive_schedule::
               test_ResponseTimeoutAfterFirstSampleUsesPercentile);
  RUN_TEST(ae::test_uap_receive_schedule::
               test_NoSyntheticSeedMeansEmptyUntilRealRtt);
  RUN_TEST(ae::test_uap_receive_schedule::test_PingDeadlineGuardFormulas);
  RUN_TEST(ae::test_uap_receive_schedule::
               test_WireAnnouncedIntervalUnchangedVsLocalSendEarlier);
  RUN_TEST(ae::test_uap_receive_schedule::
               test_RxCloseIsPongPlusWindowNotSendPlusWindow);
  RUN_TEST(ae::test_uap_receive_schedule::
               test_ConversionUsesLibraryTimePointOnly);
  RUN_TEST(ae::test_uap_receive_schedule::test_DeltaZeroYieldsUnknownDeadline);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_NegativeDeltaYieldsMissedDeadline);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_UapWireFieldOrderAndSignedInt64);
  RUN_TEST(ae::test_uap_receive_schedule::test_BenchMessageCrcRoundTrip);
  RUN_TEST(ae::test_uap_receive_schedule::test_ClassifyReceiveSendOffset);
  RUN_TEST(ae::test_uap_receive_schedule::test_EarlyRxWindowComputation);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_LocalRxWindowMonotonicAndStaleTimer);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_ServerWindowInvariantIndependentOfRtt);
  return UNITY_END();
}
