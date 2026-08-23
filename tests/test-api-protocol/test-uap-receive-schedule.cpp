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
  auto const begin = TimePoint{} + std::chrono::milliseconds{1000};
  auto const end = TimePoint{} + std::chrono::milliseconds{1100};
  auto const anchor = ComputeLocalAnchor(begin, end);
  TEST_ASSERT_TRUE(anchor == TimePoint{} + std::chrono::milliseconds{1050});

  auto const schedule =
      MakePeerReceiveSchedule(anchor, 10'000, 9'000, 5'000);
  // Absolute checks: must not clamp to TimePoint::min()/max() for normal ages.
  TEST_ASSERT_TRUE(schedule.last_ping ==
                   anchor - std::chrono::milliseconds{1000});
  TEST_ASSERT_TRUE(schedule.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*schedule.next_ping_deadline ==
                   anchor + std::chrono::milliseconds{4000});
  TEST_ASSERT_TRUE(TimePointOffsetByMs(anchor, -1'000) ==
                   anchor - std::chrono::milliseconds{1000});
  TEST_ASSERT_TRUE(TimePointOffsetByMs(anchor, 4'000) ==
                   anchor + std::chrono::milliseconds{4000});
  static_assert(std::is_same_v<decltype(schedule.last_ping), TimePoint>);
}

void test_DeltaZeroYieldsNulloptDeadline() {
  auto const schedule = MakePeerReceiveSchedule(
      TimePoint{} + std::chrono::seconds{5}, 1000, 900, 0);
  TEST_ASSERT_FALSE(schedule.next_ping_deadline.has_value());
}

void test_NegativeDeltaYieldsNulloptDeadline() {
  auto const schedule =
      MakePeerReceiveSchedule(TimePoint{}, 1000, 900, -1);
  TEST_ASSERT_FALSE(schedule.next_ping_deadline.has_value());
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
  RUN_TEST(ae::test_uap_receive_schedule::test_DeltaZeroYieldsNulloptDeadline);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_NegativeDeltaYieldsNulloptDeadline);
  RUN_TEST(
      ae::test_uap_receive_schedule::test_UapWireFieldOrderAndSignedInt64);
  RUN_TEST(ae::test_uap_receive_schedule::test_BenchMessageCrcRoundTrip);
  RUN_TEST(ae::test_uap_receive_schedule::test_ClassifyReceiveSendOffset);
  return UNITY_END();
}
