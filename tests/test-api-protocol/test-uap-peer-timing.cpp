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
#include <limits>
#include <optional>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/request_id.h"
#include "aether/receive_schedule.h"
#include "aether/types/data_buffer.h"
#include "aether/types/uid.h"
#include "aether/work_cloud_api/client_timing.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "assert_packet.h"

namespace ae::test_uap_peer_timing {
namespace {

Duration Ms(std::uint32_t v) {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{v});
}

TimePoint Tp(std::int64_t ms) {
  return TimePoint{} + std::chrono::milliseconds{ms};
}

ConvertedServerTiming Sample(std::int64_t last_ms,
                             std::optional<std::int64_t> next_ms,
                             PeerScheduleState state,
                             ServerId id = {}) {
  ConvertedServerTiming s{};
  s.server_id = id;
  s.last_online = Tp(last_ms);
  s.state = state;
  if (next_ms.has_value()) {
    s.next_ping_deadline = Tp(*next_ms);
  }
  return s;
}

}  // namespace

void test_PingMethodIdAndParamOrder() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ctx = ApiContext{api};
  ctx->ping(std::int64_t{3000}, std::int64_t{1000});
  DataBuffer packet = std::move(ctx);
  AssertPacket(packet, MessageId{4}, Skip<RequestId>{}, std::int64_t{3000},
               std::int64_t{1000});
}

void test_GetClientTimingMethodIdAndUidParam() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ctx = ApiContext{api};
  auto const uid = Uid::FromString("f81d4fae-7dec-11d0-a765-00a0c91e6bf6");
  ctx->get_client_timing(uid);
  DataBuffer packet = std::move(ctx);
  AssertPacket(packet, MessageId{35}, Skip<RequestId>{}, uid);
}

void test_GetUapRemainsMethod34AndIsNotClientTiming() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ctx = ApiContext{api};
  auto const uid = Uid::FromString("f81d4fae-7dec-11d0-a765-00a0c91e6bf6");
  ctx->get_uap(uid);
  DataBuffer packet = std::move(ctx);
  AssertPacket(packet, MessageId{34}, Skip<RequestId>{}, uid);
}

void test_PingPacketDoesNotIncludeSetNextReadDelay() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ping_only = ApiContext{api};
  ping_only->ping(std::int64_t{3000}, std::int64_t{1000});
  DataBuffer const ping_packet = std::move(ping_only);

  ProtocolContext pc2;
  AuthorizedApi api2{pc2};
  auto ping_and_delay = ApiContext{api2};
  ping_and_delay->ping(std::int64_t{3000}, std::int64_t{1000});
  ping_and_delay->set_next_read_delay(std::int64_t{3000});
  DataBuffer const both = std::move(ping_and_delay);

  AssertPacket(ping_packet, MessageId{4}, Skip<RequestId>{}, std::int64_t{3000},
               std::int64_t{1000});
  TEST_ASSERT_TRUE(ping_packet.size() < both.size());
}

void test_ClientTimingFieldOrderSignedInt64() {
  ClientTiming const timing{1'000, -250};
  std::vector<std::uint8_t> packed;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Save(timing);
  }
  TEST_ASSERT_EQUAL_UINT(16, packed.size());
  std::int64_t next_delta = 0;
  std::int64_t last_connect = 0;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Load(next_delta);
    archive.Load(last_connect);
  }
  TEST_ASSERT_TRUE(next_delta == 1'000);
  TEST_ASSERT_TRUE(last_connect == -250);
}

void test_ClientTimingNegativeZeroPositiveAndBounds() {
  ClientTiming const timing{std::numeric_limits<std::int64_t>::min(),
                            std::numeric_limits<std::int64_t>::max()};
  std::vector<std::uint8_t> packed;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Save(timing);
  }
  ClientTiming loaded{};
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Load(loaded);
  }
  TEST_ASSERT_TRUE(loaded.next_ping_delta_ms ==
                   std::numeric_limits<std::int64_t>::min());
  TEST_ASSERT_TRUE(loaded.last_connect_delta_ms ==
                   std::numeric_limits<std::int64_t>::max());
}

void test_ConversionExampleQsendMinRtt80() {
  auto const qsend = Tp(10'000);
  auto const one_way = OneWayPingEstimate(false, Ms(80));
  TEST_ASSERT_TRUE(one_way == Ms(40));
  ClientTiming timing{1'000, -200};
  auto const converted = ConvertClientTiming(qsend, one_way, timing);
  TEST_ASSERT_TRUE(converted.last_online == Tp(10'000 - 160));
  TEST_ASSERT_TRUE(converted.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*converted.next_ping_deadline == Tp(10'000 + 1'040));
  TEST_ASSERT_TRUE(converted.state == PeerScheduleState::kExpected);
}

void test_ConversionNegativeZeroEmptyStatsAndSaturation() {
  auto const qsend = Tp(5'000);
  auto const missed =
      ConvertClientTiming(qsend, Ms(40), ClientTiming{-300, -50});
  TEST_ASSERT_TRUE(missed.state == PeerScheduleState::kMissedDeadline);
  TEST_ASSERT_TRUE(missed.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*missed.next_ping_deadline == Tp(5'000 + 40 - 300));
  TEST_ASSERT_TRUE(missed.last_online == Tp(5'000 + 40 - 50));

  auto const unknown = ConvertClientTiming(qsend, Ms(40), ClientTiming{0, -10});
  TEST_ASSERT_TRUE(unknown.state == PeerScheduleState::kUnknown);
  TEST_ASSERT_FALSE(unknown.next_ping_deadline.has_value());

  TEST_ASSERT_TRUE(OneWayPingEstimate(true, Ms(80)) == Ms(100));

  auto const saturated = TimePointOffsetByMs(
      TimePoint::max(), std::numeric_limits<std::int64_t>::max());
  TEST_ASSERT_TRUE(saturated == TimePoint::max());
  auto const saturated_min = TimePointOffsetByMs(
      TimePoint::min(), std::numeric_limits<std::int64_t>::min());
  TEST_ASSERT_TRUE(saturated_min == TimePoint::min());
}

void test_AggregateFutureCases() {
  auto const one_future = AggregatePeerTimings(
      {Sample(100, 2000, PeerScheduleState::kExpected)});
  TEST_ASSERT_TRUE(one_future.has_value());
  TEST_ASSERT_TRUE(one_future->state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(one_future->next_ping_deadline == Tp(2000));

  auto const latest_future = AggregatePeerTimings(
      {Sample(1, 1000, PeerScheduleState::kExpected, 1),
       Sample(2, 2500, PeerScheduleState::kExpected, 2)});
  TEST_ASSERT_TRUE(latest_future->next_ping_deadline == Tp(2500));

  auto const future_plus_expired = AggregatePeerTimings(
      {Sample(1, 2000, PeerScheduleState::kExpected),
       Sample(2, -100, PeerScheduleState::kMissedDeadline)});
  TEST_ASSERT_TRUE(future_plus_expired->state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(future_plus_expired->next_ping_deadline == Tp(2000));

  auto const future_plus_unknown = AggregatePeerTimings(
      {Sample(1, 1000, PeerScheduleState::kExpected),
       Sample(2, std::nullopt, PeerScheduleState::kUnknown)});
  TEST_ASSERT_TRUE(future_plus_unknown->state == PeerScheduleState::kExpected);
}

void test_AggregateMissedUnknownAndErrors() {
  auto const missed = AggregatePeerTimings(
      {Sample(1, -3000, PeerScheduleState::kMissedDeadline),
       Sample(2, -500, PeerScheduleState::kMissedDeadline)});
  TEST_ASSERT_TRUE(missed->state == PeerScheduleState::kMissedDeadline);
  TEST_ASSERT_TRUE(missed->next_ping_deadline == Tp(-500));

  auto const expired_unknown = AggregatePeerTimings(
      {Sample(1, -500, PeerScheduleState::kMissedDeadline),
       Sample(2, std::nullopt, PeerScheduleState::kUnknown)});
  TEST_ASSERT_TRUE(expired_unknown->state == PeerScheduleState::kUnknown);
  TEST_ASSERT_FALSE(expired_unknown->next_ping_deadline.has_value());

  auto const all_unknown = AggregatePeerTimings(
      {Sample(1, std::nullopt, PeerScheduleState::kUnknown),
       Sample(2, std::nullopt, PeerScheduleState::kUnknown)});
  TEST_ASSERT_TRUE(all_unknown->state == PeerScheduleState::kUnknown);

  auto const one_success = AggregatePeerTimings(
      {Sample(9, -1000, PeerScheduleState::kMissedDeadline)});
  TEST_ASSERT_TRUE(one_success->state == PeerScheduleState::kMissedDeadline);

  TEST_ASSERT_FALSE(AggregatePeerTimings({}).has_value());

  PeerTimingQueryState mixed;
  mixed.Begin();
  auto const send_ok = mixed.RegisterSend(1, Tp(0), Ms(40));
  auto const send_err = mixed.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(
      mixed.ApplyTiming(1, send_ok, ClientTiming{-1000, -20}));
  TEST_ASSERT_TRUE(mixed.ApplyError(2, send_err));
  auto const mixed_agg = mixed.TryAggregate();
  TEST_ASSERT_TRUE(mixed_agg.has_value());
  TEST_ASSERT_TRUE(mixed_agg->state == PeerScheduleState::kMissedDeadline);

  PeerTimingQueryState all_err;
  all_err.Begin();
  auto const e1 = all_err.RegisterSend(1, Tp(0), Ms(40));
  auto const e2 = all_err.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(all_err.ApplyError(1, e1));
  TEST_ASSERT_TRUE(all_err.ApplyError(2, e2));
  TEST_ASSERT_FALSE(all_err.TryAggregate().has_value());
}

void test_AggregateFreshestLastOnlineIndependentOfDeadline() {
  auto const mixed = AggregatePeerTimings(
      {Sample(100, 5000, PeerScheduleState::kExpected, 1),
       Sample(900, -10, PeerScheduleState::kMissedDeadline, 2)});
  TEST_ASSERT_TRUE(mixed->last_online == Tp(900));
  TEST_ASSERT_TRUE(mixed->state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(mixed->next_ping_deadline == Tp(5000));

  auto const reversed = AggregatePeerTimings(
      {Sample(900, -10, PeerScheduleState::kMissedDeadline, 2),
       Sample(100, 5000, PeerScheduleState::kExpected, 1)});
  TEST_ASSERT_TRUE(reversed->last_online == Tp(900));
  TEST_ASSERT_TRUE(reversed->next_ping_deadline == Tp(5000));
}

void test_LifecycleOutOfOrderStaleCancelAndNoLeak() {
  PeerTimingQueryState state;
  auto const gen = state.Begin();
  TEST_ASSERT_TRUE(state.IsCurrentQuery(gen));

  auto const send_a = state.RegisterSend(1, Tp(0), Ms(40));
  auto const send_b = state.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(state.ApplyTiming(2, send_b, ClientTiming{500, -20}));
  TEST_ASSERT_TRUE(state.ApplyTiming(1, send_a, ClientTiming{1500, -10}));
  auto aggregated = state.TryAggregate();
  TEST_ASSERT_TRUE(aggregated.has_value());
  TEST_ASSERT_TRUE(aggregated->state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(aggregated->next_ping_deadline == Tp(0 + 40 + 1500));

  TEST_ASSERT_FALSE(state.ApplyTiming(1, send_a - 1, ClientTiming{9, -1}));
  TEST_ASSERT_FALSE(state.ApplyTiming(99, send_a, ClientTiming{9, -1}));

  state.Cancel();
  TEST_ASSERT_FALSE(state.ApplyTiming(1, send_a, ClientTiming{9, -1}));

  for (int i = 0; i < 1000; ++i) {
    auto const g = state.Begin();
    auto const sid = static_cast<ServerId>(i % 7 + 1);
    auto const send = state.RegisterSend(sid, Tp(i), Ms(40));
    TEST_ASSERT_TRUE(
        state.ApplyTiming(sid, send, ClientTiming{100, -5}));
    TEST_ASSERT_TRUE(state.IsCurrentQuery(g));
    TEST_ASSERT_EQUAL_UINT(1, state.attempts.size());
  }
}

}  // namespace ae::test_uap_peer_timing

int test_uap_peer_timing() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_uap_peer_timing::test_PingMethodIdAndParamOrder);
  RUN_TEST(ae::test_uap_peer_timing::test_GetClientTimingMethodIdAndUidParam);
  RUN_TEST(ae::test_uap_peer_timing::
               test_GetUapRemainsMethod34AndIsNotClientTiming);
  RUN_TEST(ae::test_uap_peer_timing::
               test_PingPacketDoesNotIncludeSetNextReadDelay);
  RUN_TEST(ae::test_uap_peer_timing::test_ClientTimingFieldOrderSignedInt64);
  RUN_TEST(ae::test_uap_peer_timing::
               test_ClientTimingNegativeZeroPositiveAndBounds);
  RUN_TEST(ae::test_uap_peer_timing::test_ConversionExampleQsendMinRtt80);
  RUN_TEST(ae::test_uap_peer_timing::
               test_ConversionNegativeZeroEmptyStatsAndSaturation);
  RUN_TEST(ae::test_uap_peer_timing::test_AggregateFutureCases);
  RUN_TEST(ae::test_uap_peer_timing::test_AggregateMissedUnknownAndErrors);
  RUN_TEST(ae::test_uap_peer_timing::
               test_AggregateFreshestLastOnlineIndependentOfDeadline);
  RUN_TEST(ae::test_uap_peer_timing::
               test_LifecycleOutOfOrderStaleCancelAndNoLeak);
  return UNITY_END();
}
