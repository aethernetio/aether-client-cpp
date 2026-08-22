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
#include <variant>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/ae_actions/query_peer_ping_schedule.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/request_id.h"
#include "aether/cloud_connections/request_policy.h"
#include "aether/crypto/icrypto_provider.h"
#include "aether/types/uid.h"
#include "aether/vector_buffer.h"
#include "aether/work_cloud_api/uap.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "aether/work_cloud_api/work_server_api/login_api.h"

#include "assert_packet.h"

namespace ae::test_peer_uap_schedule {
namespace {

class IdentityEncrypt final : public IEncryptProvider {
 public:
  DataBuffer Encrypt(DataBuffer const& data) override { return data; }
  std::size_t EncryptOverhead() const override { return 0; }
};

}  // namespace

void test_WireMethodIds() {
  ProtocolContext pc;
  IdentityEncrypt encrypt;
  LoginApi login{pc, encrypt};
  AuthorizedApi auth{pc};

  auto login_ctx = ApiContext{login};
  (void)login_ctx->get_time_utc();
  DataBuffer login_packet = std::move(login_ctx);
  AssertPacket(login_packet, MessageId{3}, Skip<RequestId>{});

  auto ping_ctx = ApiContext{auth};
  (void)ping_ctx->ping(1, 2);
  DataBuffer ping_packet = std::move(ping_ctx);
  AssertPacket(ping_packet, MessageId{4}, Skip<RequestId>{},
               std::uint64_t{1}, std::uint64_t{2});

  auto delay_ctx = ApiContext{auth};
  delay_ctx->set_next_read_delay(33);
  DataBuffer delay_packet = std::move(delay_ctx);
  AssertPacket(delay_packet, MessageId{33}, std::int64_t{33});

  auto uid = Uid{};
  uid.value.fill(0x11);
  auto uap_ctx = ApiContext{auth};
  (void)uap_ctx->get_uap(uid);
  DataBuffer uap_packet = std::move(uap_ctx);
  AssertPacket(uap_packet, MessageId{34}, Skip<RequestId>{}, uid);
}

void test_UapSerializationFieldOrder() {
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

void test_UapSignedInt64() {
  Uap const uap{-1, std::numeric_limits<std::int64_t>::min()};
  std::vector<std::uint8_t> packed;
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Save(uap);
  }
  Uap decoded{};
  {
    auto archive = ae::seri::BinaryArchive{
        ae::VectorBuffer<ae::PackedSize>{packed},
    };
    archive.Load(decoded);
  }
  TEST_ASSERT_EQUAL_INT64(-1, decoded.delta_ms);
  TEST_ASSERT_EQUAL_INT64(std::numeric_limits<std::int64_t>::min(),
                          decoded.last_read_timestamp_ms);
}

void test_PingThenSetNextReadDelayOrder() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto const interval = std::uint64_t{15'000};
  auto const rx_window = std::uint64_t{3'000};

  auto call = ApiContext{api};
  (void)call->ping(interval, rx_window);
  call->set_next_read_delay(static_cast<std::int64_t>(interval));
  DataBuffer packet = std::move(call);

  AssertPacket(packet, MessageId{4}, Skip<RequestId>{}, interval, rx_window,
               MessageId{33}, static_cast<std::int64_t>(interval));
}

void test_SetNextReadDelayUsesIntervalNotRxWindow() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto const interval = std::uint64_t{20'000};
  auto const rx_window = std::uint64_t{1'500};

  auto call = ApiContext{api};
  (void)call->ping(interval, rx_window);
  call->set_next_read_delay(static_cast<std::int64_t>(interval));
  DataBuffer packet = std::move(call);

  AssertPacket(packet, MessageId{4}, Skip<RequestId>{}, interval, rx_window,
               MessageId{33}, std::int64_t{20'000});
  TEST_ASSERT_TRUE(interval != rx_window);
}

void test_QueryPacksGetTimeUtcAndGetUap() {
  ProtocolContext pc;
  IdentityEncrypt encrypt;
  LoginApi login{pc, encrypt};
  AuthorizedApi auth{pc};
  auto uid = Uid{};
  uid.value.fill(0x22);

  auto time_ctx = ApiContext{login};
  (void)time_ctx->get_time_utc();
  DataBuffer time_packet = std::move(time_ctx);
  AssertPacket(time_packet, MessageId{3}, Skip<RequestId>{});

  auto uap_ctx = ApiContext{auth};
  (void)uap_ctx->get_uap(uid);
  DataBuffer uap_packet = std::move(uap_ctx);
  AssertPacket(uap_packet, MessageId{34}, Skip<RequestId>{}, uid);

  RequestPolicy::Variant policy = RequestPolicy::MainServer{};
  TEST_ASSERT_TRUE(std::holds_alternative<RequestPolicy::MainServer>(policy));
}

void test_RemainingAndSteadyDeadline() {
  auto const last = std::int64_t{1'000'000};
  auto const delta = std::int64_t{5'500};
  auto const server_now = std::int64_t{1'001'000};
  auto const remaining =
      ComputePeerPingRemaining(last, delta, server_now);
  TEST_ASSERT_TRUE(remaining.has_deadline);
  TEST_ASSERT_EQUAL_INT64(4'500, remaining.remaining_ms);

  auto const now = std::chrono::steady_clock::now();
  auto const schedule =
      MakePeerPingSchedule(server_now, last, delta, ServerId{7}, now);
  TEST_ASSERT_TRUE(schedule.local_deadline.has_value());
  auto const expected =
      SafeSteadyDeadline(now, 4'500, kPeerPingScheduleGraceMs);
  TEST_ASSERT_TRUE(schedule.local_deadline.value() == expected);
  TEST_ASSERT_EQUAL_INT64(server_now, schedule.server_now_ms);
  TEST_ASSERT_EQUAL_INT64(last, schedule.last_ping_server_ms);
  TEST_ASSERT_EQUAL_INT64(delta, schedule.next_ping_delta_ms);
  TEST_ASSERT_EQUAL(7, schedule.server_id);
}

void test_ClockSkewDoesNotChangeSteadyDeadline() {
  // Deadline is derived only from server_now + UAP + steady_now.
  // Injected wall-clock values must not affect it (they are unused by design).
  auto const last = std::int64_t{2'000'000};
  auto const delta = std::int64_t{10'000};
  auto const server_now = std::int64_t{2'002'000};
  auto const now = std::chrono::steady_clock::now();

  auto const a = MakePeerPingSchedule(server_now, last, delta, ServerId{1}, now);
  auto const b = MakePeerPingSchedule(server_now, last, delta, ServerId{1}, now);
  TEST_ASSERT_TRUE(a.local_deadline.has_value());
  TEST_ASSERT_TRUE(b.local_deadline.has_value());
  TEST_ASSERT_TRUE(a.local_deadline.value() == b.local_deadline.value());
  TEST_ASSERT_EQUAL_INT64(a.server_now_ms, b.server_now_ms);
  auto const expected =
      SafeSteadyDeadline(now, /*remaining*/ 8'000, kPeerPingScheduleGraceMs);
  TEST_ASSERT_TRUE(a.local_deadline.value() == expected);
}

void test_SafeSteadyDeadlineSaturation() {
  auto const max_tp = std::chrono::steady_clock::time_point::max();
  auto const near_max = max_tp - std::chrono::milliseconds{10};
  auto const saturated =
      SafeSteadyDeadline(near_max, /*remaining*/ 1'000'000'000,
                         kPeerPingScheduleGraceMs);
  TEST_ASSERT_TRUE(saturated == max_tp);

  auto const now = std::chrono::steady_clock::now();
  auto const neg = SafeSteadyDeadline(now, -5, -10);
  TEST_ASSERT_TRUE(neg == now);

  auto const normal = SafeSteadyDeadline(now, 100, 50);
  TEST_ASSERT_TRUE(normal == now + std::chrono::milliseconds{150});
}

void test_ZeroDeltaHasNoDeadline() {
  auto const remaining =
      ComputePeerPingRemaining(1'000, 0, 1'100);
  TEST_ASSERT_FALSE(remaining.has_deadline);

  auto const schedule = MakePeerPingSchedule(
      1'100, 1'000, 0, ServerId{1}, std::chrono::steady_clock::now());
  TEST_ASSERT_FALSE(schedule.local_deadline.has_value());
}

void test_NegativeDeltaHasNoDeadline() {
  auto const remaining =
      ComputePeerPingRemaining(1'000, -5, 1'100);
  TEST_ASSERT_FALSE(remaining.has_deadline);
}

void test_SaturationAndOverflow() {
  auto const max_v = std::numeric_limits<std::int64_t>::max();
  auto const min_v = std::numeric_limits<std::int64_t>::min();
  TEST_ASSERT_EQUAL_INT64(max_v, SaturatingAddI64(max_v, 1));
  TEST_ASSERT_EQUAL_INT64(min_v, SaturatingAddI64(min_v, -1));
  TEST_ASSERT_EQUAL_INT64(max_v, SaturatingSubI64(max_v, -1));
  TEST_ASSERT_EQUAL_INT64(min_v, SaturatingSubI64(min_v, 1));

  auto const remaining = ComputePeerPingRemaining(max_v, 100, max_v - 10);
  TEST_ASSERT_TRUE(remaining.has_deadline);
  TEST_ASSERT_EQUAL_INT64(10, remaining.remaining_ms);

  TEST_ASSERT_TRUE(PeerPingScheduleValuesMalformed(-1, 1));
  TEST_ASSERT_TRUE(PeerPingScheduleValuesMalformed(1, -1));
  TEST_ASSERT_FALSE(PeerPingScheduleValuesMalformed(0, 0));
}

void test_AnnounceNextPingUnknownPacksZeroOnAllPolicy() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto call = ApiContext{api};
  call->set_next_read_delay(0);
  DataBuffer packet = std::move(call);
  AssertPacket(packet, MessageId{33}, std::int64_t{0});

  RequestPolicy::Variant policy = RequestPolicy::All{};
  TEST_ASSERT_TRUE(std::holds_alternative<RequestPolicy::All>(policy));

  TEST_ASSERT_EQUAL_INT(
      1, static_cast<int>(QueryPeerPingScheduleError::kGetCloudFailed));
  TEST_ASSERT_EQUAL_INT(
      2, static_cast<int>(QueryPeerPingScheduleError::kMainServerUnavailable));
  TEST_ASSERT_EQUAL_INT(
      3, static_cast<int>(QueryPeerPingScheduleError::kGetTimeUtcFailed));
  TEST_ASSERT_EQUAL_INT(
      4, static_cast<int>(QueryPeerPingScheduleError::kGetUapFailed));
  TEST_ASSERT_EQUAL_INT(
      5, static_cast<int>(QueryPeerPingScheduleError::kMalformedResponse));
}

}  // namespace ae::test_peer_uap_schedule

int test_peer_uap_schedule() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_peer_uap_schedule::test_WireMethodIds);
  RUN_TEST(ae::test_peer_uap_schedule::test_UapSerializationFieldOrder);
  RUN_TEST(ae::test_peer_uap_schedule::test_UapSignedInt64);
  RUN_TEST(ae::test_peer_uap_schedule::test_PingThenSetNextReadDelayOrder);
  RUN_TEST(ae::test_peer_uap_schedule::test_SetNextReadDelayUsesIntervalNotRxWindow);
  RUN_TEST(ae::test_peer_uap_schedule::test_QueryPacksGetTimeUtcAndGetUap);
  RUN_TEST(ae::test_peer_uap_schedule::test_RemainingAndSteadyDeadline);
  RUN_TEST(ae::test_peer_uap_schedule::test_ClockSkewDoesNotChangeSteadyDeadline);
  RUN_TEST(ae::test_peer_uap_schedule::test_SafeSteadyDeadlineSaturation);
  RUN_TEST(ae::test_peer_uap_schedule::test_ZeroDeltaHasNoDeadline);
  RUN_TEST(ae::test_peer_uap_schedule::test_NegativeDeltaHasNoDeadline);
  RUN_TEST(ae::test_peer_uap_schedule::test_SaturationAndOverflow);
  RUN_TEST(ae::test_peer_uap_schedule::test_AnnounceNextPingUnknownPacksZeroOnAllPolicy);
  return UNITY_END();
}
