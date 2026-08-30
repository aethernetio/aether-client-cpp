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
#include "aether/cloud_connections/cloud_request.h"
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

  TEST_ASSERT_FALSE(
      AggregatePeerTimings(std::vector<ConvertedServerTiming>{}).has_value());

  PeerTimingQueryState mixed;
  mixed.Begin();
  auto const send_ok = mixed.RegisterSend(1, Tp(0), Ms(40));
  auto const send_err = mixed.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(
      mixed.ApplyTiming(1, send_ok, ClientTiming{-1000, -20}));
  TEST_ASSERT_TRUE(mixed.ApplyError(2, send_err));
  auto const mixed_agg = mixed.TryAggregate();
  TEST_ASSERT_TRUE(mixed_agg.has_value());
  TEST_ASSERT_TRUE(mixed_agg->state == PeerScheduleState::kUnknown);
  TEST_ASSERT_FALSE(mixed_agg->next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(mixed.ReadyToComplete());

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

void test_ConservativeMatrixAndExpectedSnapshot() {
  PeerTimingQueryState future_err;
  future_err.Begin({1, 2});
  auto const f1 = future_err.RegisterSend(1, Tp(0), Ms(40));
  auto const f2 = future_err.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(future_err.ApplyTiming(1, f1, ClientTiming{1500, -10}));
  TEST_ASSERT_TRUE(future_err.ApplyError(2, f2));
  auto const fe = future_err.TryAggregate();
  TEST_ASSERT_TRUE(fe.has_value());
  TEST_ASSERT_TRUE(fe->state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(fe->next_ping_deadline.has_value());

  PeerTimingQueryState unknown_err;
  unknown_err.Begin({1, 2});
  auto const u1 = unknown_err.RegisterSend(1, Tp(0), Ms(40));
  auto const u2 = unknown_err.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(unknown_err.ApplyTiming(1, u1, ClientTiming{0, -10}));
  TEST_ASSERT_TRUE(unknown_err.ApplyError(2, u2));
  auto const ue = unknown_err.TryAggregate();
  TEST_ASSERT_TRUE(ue.has_value());
  TEST_ASSERT_TRUE(ue->state == PeerScheduleState::kUnknown);

  PeerTimingQueryState snapshot;
  snapshot.Begin({20, 21, 22});
  auto const s20 = snapshot.RegisterSend(20, Tp(0), Ms(40));
  auto const s21 = snapshot.RegisterSend(21, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(snapshot.ApplyTiming(20, s20, ClientTiming{-1000, -5}));
  TEST_ASSERT_TRUE(snapshot.ApplyTiming(21, s21, ClientTiming{-800, -8}));
  TEST_ASSERT_FALSE(snapshot.ReadyToComplete());
  auto const unresolved = snapshot.TryAggregate();
  TEST_ASSERT_TRUE(unresolved.has_value());
  TEST_ASSERT_TRUE(unresolved->state == PeerScheduleState::kUnknown);
  TEST_ASSERT_TRUE(snapshot.ApplyError(22, snapshot.RegisterSend(22, Tp(0), Ms(40))));
  TEST_ASSERT_TRUE(snapshot.ReadyToComplete());
  auto const snap_done = snapshot.TryAggregate();
  TEST_ASSERT_TRUE(snap_done.has_value());
  TEST_ASSERT_TRUE(snap_done->state == PeerScheduleState::kUnknown);

  PeerTimingQueryState incomplete;
  incomplete.Begin({20, 21}, true);
  auto const i20 = incomplete.RegisterSend(20, Tp(0), Ms(40));
  auto const i21 = incomplete.RegisterSend(21, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(incomplete.ApplyTiming(20, i20, ClientTiming{-1000, -5}));
  TEST_ASSERT_TRUE(incomplete.ApplyTiming(21, i21, ClientTiming{-800, -8}));
  auto const inc = incomplete.TryAggregate();
  TEST_ASSERT_TRUE(inc.has_value());
  TEST_ASSERT_TRUE(inc->state == PeerScheduleState::kUnknown);

  PeerTimingQueryState all_neg;
  all_neg.Begin({1, 2});
  auto const n1 = all_neg.RegisterSend(1, Tp(0), Ms(40));
  auto const n2 = all_neg.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(all_neg.ApplyTiming(1, n1, ClientTiming{-1000, -20}));
  TEST_ASSERT_TRUE(all_neg.ApplyTiming(2, n2, ClientTiming{-400, -50}));
  auto const missed = all_neg.TryAggregate();
  TEST_ASSERT_TRUE(missed.has_value());
  TEST_ASSERT_TRUE(missed->state == PeerScheduleState::kMissedDeadline);
  TEST_ASSERT_TRUE(missed->next_ping_deadline == Tp(0 + 40 - 400));
  TEST_ASSERT_TRUE(missed->last_online == Tp(0 + 40 - 20));
}

void test_RetryRaceAndPostSuccessNoRemake() {
  PeerTimingQueryOrchestrator orch;
  orch.Start({1, 2});
  auto const a1 = orch.Send(1, Tp(0), Ms(40));
  auto const b1 = orch.Send(2, Tp(0), Ms(40));
  orch.OnTransient(2, b1);
  TEST_ASSERT_EQUAL_INT(0, orch.callback_count);
  TEST_ASSERT_FALSE(orch.state.ReadyToComplete());
  orch.OnSuccess(1, a1, ClientTiming{-1000, -10});
  TEST_ASSERT_EQUAL_INT(0, orch.callback_count);
  auto const b2 = orch.Send(2, Tp(10), Ms(40));
  orch.OnSuccess(2, b2, ClientTiming{2000, -15});
  TEST_ASSERT_EQUAL_INT(1, orch.callback_count);
  TEST_ASSERT_TRUE(orch.last_schedule.has_value());
  TEST_ASSERT_TRUE(orch.last_schedule->state == PeerScheduleState::kExpected);

  PeerTimingQueryOrchestrator reverse;
  reverse.Start({1, 2});
  auto const ra = reverse.Send(1, Tp(0), Ms(40));
  reverse.OnSuccess(1, ra, ClientTiming{-1000, -10});
  TEST_ASSERT_EQUAL_INT(0, reverse.callback_count);
  auto const rb = reverse.Send(2, Tp(0), Ms(40));
  reverse.OnTransient(2, rb);
  TEST_ASSERT_EQUAL_INT(0, reverse.callback_count);
  auto const rb2 = reverse.Send(2, Tp(10), Ms(40));
  reverse.OnSuccess(2, rb2, ClientTiming{2000, -15});
  TEST_ASSERT_EQUAL_INT(1, reverse.callback_count);
  TEST_ASSERT_TRUE(reverse.last_schedule->state == PeerScheduleState::kExpected);

  PeerTimingQueryState post;
  post.Begin({1, 2});
  auto const p1 = post.RegisterSend(1, Tp(0), Ms(40));
  auto const p2 = post.RegisterSend(2, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(post.ApplyTiming(1, p1, ClientTiming{1200, -10}));
  auto const p1_again = post.RegisterSend(1, Tp(100), Ms(40));
  TEST_ASSERT_EQUAL_UINT(p1, p1_again);
  TEST_ASSERT_TRUE(post.attempts[1].status ==
                   ServerTimingAttemptStatus::kSuccess);
  TEST_ASSERT_FALSE(post.ApplyError(1, p1));
  TEST_ASSERT_TRUE(post.attempts[1].converted.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(post.ApplyTiming(2, p2, ClientTiming{800, -20}));
  auto const done = post.TryAggregate();
  TEST_ASSERT_TRUE(done->state == PeerScheduleState::kExpected);
}

void test_QuarantinedServerExcludedFromQuerySetAllowsMissedDeadline() {
  std::vector<ServerId> query_set;
  auto const cov = BuildPeerTimingQuerySet(
      {
          SelectedServerSnapshotItem{20, false, true},
          SelectedServerSnapshotItem{21, false, true},
          SelectedServerSnapshotItem{22, true, true},
      },
      query_set);
  TEST_ASSERT_EQUAL_UINT(3, cov.selected_server_count);
  TEST_ASSERT_EQUAL_UINT(1, cov.quarantined_skipped_count);
  TEST_ASSERT_EQUAL_UINT(2, cov.queried_server_count);
  TEST_ASSERT_EQUAL_UINT(2, query_set.size());

  PeerTimingQueryState st;
  st.Begin(query_set, false, cov);
  auto const q20 = st.RegisterSend(20, Tp(0), Ms(40));
  auto const q21 = st.RegisterSend(21, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(st.ApplyTiming(20, q20, ClientTiming{-1000, -5}));
  TEST_ASSERT_TRUE(st.ApplyTiming(21, q21, ClientTiming{-800, -8}));
  TEST_ASSERT_TRUE(st.attempts.find(22) == st.attempts.end());
  auto const missed = st.TryAggregate();
  TEST_ASSERT_TRUE(missed.has_value());
  TEST_ASSERT_TRUE(missed->state == PeerScheduleState::kMissedDeadline);
  auto const got = st.QueryCoverage();
  TEST_ASSERT_EQUAL_UINT(0, got.failed_server_count);
  TEST_ASSERT_EQUAL_UINT(2, got.successful_server_count);
  TEST_ASSERT_EQUAL_UINT(1, got.quarantined_skipped_count);
}

void test_ActiveServerQueryErrorGivesUnknown() {
  PeerTimingQueryState st;
  st.Begin({20, 21, 22});
  auto const q20 = st.RegisterSend(20, Tp(0), Ms(40));
  auto const q21 = st.RegisterSend(21, Tp(0), Ms(40));
  auto const q22 = st.RegisterSend(22, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(st.ApplyTiming(20, q20, ClientTiming{-1000, -5}));
  TEST_ASSERT_TRUE(st.ApplyTiming(21, q21, ClientTiming{-800, -8}));
  TEST_ASSERT_TRUE(st.ApplyTerminalError(22, q22));
  auto const unknown = st.TryAggregate();
  TEST_ASSERT_TRUE(unknown.has_value());
  TEST_ASSERT_TRUE(unknown->state == PeerScheduleState::kUnknown);
}

void test_ServerLeavingQuarantineFreshQueryCanBeExpected() {
  PeerTimingQueryState st;
  st.Begin({20, 21, 22});
  auto const q20 = st.RegisterSend(20, Tp(0), Ms(40));
  auto const q21 = st.RegisterSend(21, Tp(0), Ms(40));
  auto const q22 = st.RegisterSend(22, Tp(0), Ms(40));
  TEST_ASSERT_TRUE(st.ApplyTiming(20, q20, ClientTiming{-1000, -5}));
  TEST_ASSERT_TRUE(st.ApplyTiming(21, q21, ClientTiming{-800, -8}));
  TEST_ASSERT_TRUE(st.ApplyTiming(22, q22, ClientTiming{2000, -7}));
  auto const expected = st.TryAggregate();
  TEST_ASSERT_TRUE(expected.has_value());
  TEST_ASSERT_TRUE(expected->state == PeerScheduleState::kExpected);
}

void test_AllSelectedServersQuarantinedYieldsEmptyQuerySet() {
  std::vector<ServerId> query_set;
  auto const cov = BuildPeerTimingQuerySet(
      {
          SelectedServerSnapshotItem{20, true, true},
          SelectedServerSnapshotItem{21, true, true},
          SelectedServerSnapshotItem{22, true, true},
      },
      query_set);
  TEST_ASSERT_EQUAL_UINT(0, cov.queried_server_count);
  TEST_ASSERT_EQUAL_UINT(3, cov.quarantined_skipped_count);
  TEST_ASSERT_TRUE(query_set.empty());
}

void test_SingleServerScheduleConversionStates() {
  auto const qsend = Tp(10'000);
  auto const one_way = Ms(40);

  auto const expected =
      ToPeerReceiveSchedule(ConvertClientTiming(
          qsend, one_way, ClientTiming{1'000, -200}, /*server_id=*/7));
  TEST_ASSERT_TRUE(expected.state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(expected.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*expected.next_ping_deadline == Tp(10'000 + 40 + 1'000));
  TEST_ASSERT_TRUE(expected.last_online == Tp(10'000 + 40 - 200));

  auto const missed = ToPeerReceiveSchedule(
      ConvertClientTiming(qsend, one_way, ClientTiming{-300, -50}, 8));
  TEST_ASSERT_TRUE(missed.state == PeerScheduleState::kMissedDeadline);
  TEST_ASSERT_TRUE(missed.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(*missed.next_ping_deadline == Tp(10'000 + 40 - 300));
  TEST_ASSERT_TRUE(missed.last_online == Tp(10'000 + 40 - 50));

  auto const unknown = ToPeerReceiveSchedule(
      ConvertClientTiming(qsend, one_way, ClientTiming{0, -10}, 9));
  TEST_ASSERT_TRUE(unknown.state == PeerScheduleState::kUnknown);
  TEST_ASSERT_FALSE(unknown.next_ping_deadline.has_value());
  TEST_ASSERT_TRUE(unknown.last_online == Tp(10'000 + 40 - 10));
}

void test_SingleServerScheduleHasNoCrossServerAggregation() {
  // Server-scoped conversion ignores other servers entirely.
  auto const only_a = ToPeerReceiveSchedule(ConvertClientTiming(
      Tp(0), Ms(40), ClientTiming{-1000, -20}, /*server_id=*/1));
  auto const only_b = ToPeerReceiveSchedule(ConvertClientTiming(
      Tp(0), Ms(40), ClientTiming{2000, -5}, /*server_id=*/2));
  TEST_ASSERT_TRUE(only_a.state == PeerScheduleState::kMissedDeadline);
  TEST_ASSERT_TRUE(only_b.state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(only_a.last_online != only_b.last_online);
}

void test_PresenceOrOnlineAndEarlyCompletion() {
  auto const online = AggregatePeerPresence(
      {Sample(100, 2000, PeerScheduleState::kExpected, 1),
       Sample(50, -100, PeerScheduleState::kMissedDeadline, 2),
       Sample(60, -200, PeerScheduleState::kMissedDeadline, 3)});
  TEST_ASSERT_TRUE(online.has_value());
  TEST_ASSERT_TRUE(online->state == PeerPresenceState::kOnline);
  TEST_ASSERT_TRUE(online->next_ping_deadline == Tp(2000));
  TEST_ASSERT_TRUE(online->last_online == Tp(100));

  PeerPresenceQueryOrchestrator early;
  early.Start({1, 2, 3});
  auto const a = early.Send(1, Tp(0), Ms(40));
  early.OnSuccess(1, a, ClientTiming{1500, -10});
  TEST_ASSERT_EQUAL_INT(1, early.callback_count);
  TEST_ASSERT_TRUE(early.last_presence.has_value());
  TEST_ASSERT_TRUE(early.last_presence->state == PeerPresenceState::kOnline);
  TEST_ASSERT_FALSE(early.state.ReadyToComplete());  // B,C still pending

  auto const online_with_error = AggregatePeerPresence(
      PeerTimingAggregateContext{
          .expected_server_count = 3,
          .success_count = 2,
          .terminal_error_count = 1,
          .unresolved_count = 0,
          .snapshot_incomplete = false,
          .successes =
              {
                  Sample(1, -500, PeerScheduleState::kMissedDeadline, 1),
                  Sample(2, 3000, PeerScheduleState::kExpected, 2),
              },
      });
  TEST_ASSERT_TRUE(online_with_error->state == PeerPresenceState::kOnline);
}

void test_PresenceOfflineRequiresAllMissed() {
  auto const offline = AggregatePeerPresence(
      {Sample(1, -3000, PeerScheduleState::kMissedDeadline),
       Sample(2, -500, PeerScheduleState::kMissedDeadline),
       Sample(3, -100, PeerScheduleState::kMissedDeadline)});
  TEST_ASSERT_TRUE(offline->state == PeerPresenceState::kOffline);
  TEST_ASSERT_TRUE(offline->next_ping_deadline == Tp(-100));

  PeerTimingAggregateContext fail_blocks_offline;
  fail_blocks_offline.expected_server_count = 3;
  fail_blocks_offline.success_count = 2;
  fail_blocks_offline.terminal_error_count = 1;
  fail_blocks_offline.successes = {
      Sample(1, -3000, PeerScheduleState::kMissedDeadline),
      Sample(2, -500, PeerScheduleState::kMissedDeadline),
  };
  auto const unknown_fail = AggregatePeerPresence(fail_blocks_offline);
  TEST_ASSERT_TRUE(unknown_fail->state == PeerPresenceState::kUnknown);

  auto const unknown_sample = AggregatePeerPresence(
      {Sample(1, -3000, PeerScheduleState::kMissedDeadline),
       Sample(2, std::nullopt, PeerScheduleState::kUnknown),
       Sample(3, -100, PeerScheduleState::kMissedDeadline)});
  TEST_ASSERT_TRUE(unknown_sample->state == PeerPresenceState::kUnknown);

  auto const all_unknown = AggregatePeerPresence(
      {Sample(1, std::nullopt, PeerScheduleState::kUnknown),
       Sample(2, std::nullopt, PeerScheduleState::kUnknown)});
  TEST_ASSERT_TRUE(all_unknown->state == PeerPresenceState::kUnknown);

  TEST_ASSERT_FALSE(
      AggregatePeerPresence(std::vector<ConvertedServerTiming>{}).has_value());
}

void test_PresenceLatestLastOnline() {
  auto const mixed = AggregatePeerPresence(
      {Sample(100, 5000, PeerScheduleState::kExpected, 1),
       Sample(150, -10, PeerScheduleState::kMissedDeadline, 2),
       Sample(120, -20, PeerScheduleState::kMissedDeadline, 3)});
  TEST_ASSERT_TRUE(mixed->last_online == Tp(150));
  TEST_ASSERT_TRUE(mixed->state == PeerPresenceState::kOnline);
}

void test_ObserverReceiveScheduleDoesNotAffectPeerClassification() {
  // ConvertClientTiming takes only qsend/one_way/ClientTiming — never
  // observer ping_interval or receive_window. Changing observer schedule
  // values must not change the classification of identical server timing.
  ReceiveSchedule observer_fast{.ping_interval = Ms(1000),
                                .receive_window = Ms(1000)};
  ReceiveSchedule observer_slow{.ping_interval = Ms(60000),
                                .receive_window = Ms(60000)};
  static_cast<void>(observer_fast);
  static_cast<void>(observer_slow);

  ClientTiming const peer_timing{2'000, -100};
  auto const a = ConvertClientTiming(Tp(5'000), Ms(40), peer_timing, 1);
  auto const b = ConvertClientTiming(Tp(5'000), Ms(40), peer_timing, 1);
  TEST_ASSERT_TRUE(a.state == b.state);
  TEST_ASSERT_TRUE(a.state == PeerScheduleState::kExpected);
  TEST_ASSERT_TRUE(a.next_ping_deadline == b.next_ping_deadline);
  TEST_ASSERT_TRUE(a.last_online == b.last_online);

  auto const presence = AggregatePeerPresence({a, b});
  TEST_ASSERT_TRUE(presence->state == PeerPresenceState::kOnline);
}

void test_ThousandQueriesDestroyPendingAndCloudRequestFlags() {
  PeerTimingQueryOrchestrator orch;
  for (int i = 0; i < 1000; ++i) {
    orch.Start({1, 2});
    auto const a = orch.Send(1, Tp(i), Ms(40));
    auto const b = orch.Send(2, Tp(i), Ms(40));
    orch.OnSuccess(1, a, ClientTiming{100, -5});
    orch.OnSuccess(2, b, ClientTiming{200, -6});
    TEST_ASSERT_EQUAL_INT(1, orch.callback_count);
    TEST_ASSERT_EQUAL_INT(1, orch.state.user_callback_count);
  }

  PeerTimingQueryOrchestrator pending;
  pending.Start({1, 2});
  auto const pa = pending.Send(1, Tp(0), Ms(40));
  pending.Destroy();
  pending.OnSuccess(1, pa, ClientTiming{100, -5});
  TEST_ASSERT_EQUAL_INT(0, pending.callback_count);

  CloudRequestAttemptState attempt;
  TEST_ASSERT_FALSE(attempt.ShouldSkipMake());
  attempt.MarkSucceeded();
  TEST_ASSERT_TRUE(attempt.ShouldSkipMake());
  TEST_ASSERT_FALSE(attempt.MarkFailed(5));
  TEST_ASSERT_FALSE(attempt.exhausted);

  CloudRequestAttemptState fail;
  TEST_ASSERT_FALSE(fail.MarkFailed(2));
  TEST_ASSERT_TRUE(fail.MarkFailed(2));
  TEST_ASSERT_TRUE(fail.ShouldSkipMake());
  TEST_ASSERT_TRUE(CloudRequestShouldFailAll(false, false));
  TEST_ASSERT_FALSE(CloudRequestShouldFailAll(false, true));
  TEST_ASSERT_FALSE(CloudRequestShouldFailAll(true, false));
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
  RUN_TEST(ae::test_uap_peer_timing::test_ConservativeMatrixAndExpectedSnapshot);
  RUN_TEST(ae::test_uap_peer_timing::test_RetryRaceAndPostSuccessNoRemake);
  RUN_TEST(ae::test_uap_peer_timing::
               test_ThousandQueriesDestroyPendingAndCloudRequestFlags);
  RUN_TEST(ae::test_uap_peer_timing::
               test_QuarantinedServerExcludedFromQuerySetAllowsMissedDeadline);
  RUN_TEST(ae::test_uap_peer_timing::test_ActiveServerQueryErrorGivesUnknown);
  RUN_TEST(ae::test_uap_peer_timing::
               test_ServerLeavingQuarantineFreshQueryCanBeExpected);
  RUN_TEST(ae::test_uap_peer_timing::
               test_AllSelectedServersQuarantinedYieldsEmptyQuerySet);
  RUN_TEST(ae::test_uap_peer_timing::test_SingleServerScheduleConversionStates);
  RUN_TEST(ae::test_uap_peer_timing::
               test_SingleServerScheduleHasNoCrossServerAggregation);
  RUN_TEST(ae::test_uap_peer_timing::test_PresenceOrOnlineAndEarlyCompletion);
  RUN_TEST(ae::test_uap_peer_timing::test_PresenceOfflineRequiresAllMissed);
  RUN_TEST(ae::test_uap_peer_timing::test_PresenceLatestLastOnline);
  RUN_TEST(ae::test_uap_peer_timing::
               test_ObserverReceiveScheduleDoesNotAffectPeerClassification);
  return UNITY_END();
}
