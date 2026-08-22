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
#include <type_traits>
#include <vector>

#include "aether/cloud_connections/ping_bench_hooks.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "examples/benches/aether_delivery_window_bench/common/bench_buckets.h"
#include "examples/benches/aether_delivery_window_bench/common/bench_classify.h"
#include "examples/benches/aether_delivery_window_bench/common/bench_ipc.h"
#include "examples/benches/aether_delivery_window_bench/common/bench_message.h"
#include "examples/benches/aether_delivery_window_bench/common/bench_stats.h"

namespace ae::test_delivery_window_bench {
namespace {

template <typename T>
struct MethodId;

template <MessageId Id, typename Sig, typename ArgProc>
struct MethodId<Method<Id, Sig, ArgProc>> {
  static constexpr MessageId value = Id;
};

void test_bench_message_crc_roundtrip() {
  using ae::bench::dw::BenchMessage;
  using ae::bench::dw::DecodeBenchMessage;
  using ae::bench::dw::SerializeBenchMessage;

  BenchMessage msg{};
  msg.run_id_hash = 0x12345678u;
  msg.config_id = 42;
  msg.cycle_id = 7;
  msg.message_id = 99;
  msg.direction = 1;
  auto bytes = SerializeBenchMessage(msg);
  BenchMessage out{};
  TEST_ASSERT_TRUE(DecodeBenchMessage(bytes.data(), bytes.size(), out));
  TEST_ASSERT_EQUAL_UINT32(msg.run_id_hash, out.run_id_hash);
  TEST_ASSERT_EQUAL_UINT32(msg.message_id, out.message_id);
  TEST_ASSERT_EQUAL_UINT8(msg.direction, out.direction);

  bytes[10] ^= 0xFFu;
  TEST_ASSERT_FALSE(DecodeBenchMessage(bytes.data(), bytes.size(), out));
}

void test_classification_boundaries() {
  using ae::bench::dw::Classification;
  using ae::bench::dw::ClassifyInput;
  using ae::bench::dw::ClassifySample;

  ClassifyInput in;
  in.window_open_us = 1'000'000;
  in.window_close_us = 2'000'000;
  in.server_accept_us = 1'100'000;
  in.receive_us = 1'200'000;
  in.next_window_open_us = 5'000'000;
  in.actual_interval_ms = 4000;
  TEST_ASSERT_EQUAL(static_cast<int>(Classification::kPushInsideWindow),
                    static_cast<int>(ClassifySample(in)));

  in.receive_us = 5'050'000;
  TEST_ASSERT_EQUAL(
      static_cast<int>(Classification::kDeferredDespiteActiveWindow),
      static_cast<int>(ClassifySample(in)));

  in.server_accept_us = 2'500'000;
  in.receive_us = 3'000'000;
  TEST_ASSERT_EQUAL(static_cast<int>(Classification::kPushOutsideWindow),
                    static_cast<int>(ClassifySample(in)));

  in.receive_us = 5'100'000;
  TEST_ASSERT_EQUAL(static_cast<int>(Classification::kDeliveredAtNextWindow),
                    static_cast<int>(ClassifySample(in)));

  in.receive_us = 7'000'000;
  TEST_ASSERT_EQUAL(static_cast<int>(Classification::kLateAfterNextWindow),
                    static_cast<int>(ClassifySample(in)));
}

void test_invalid_accept_bucket() {
  using ae::bench::dw::AcceptMatchesTargetBucket;
  using ae::bench::dw::Bucket;
  using ae::bench::dw::ClassifyAcceptBucket;

  auto actual = ClassifyAcceptBucket(
      /*accept*/ 1'000'000 + 900'000, /*open*/ 1'000'000, /*close*/ 2'000'000,
      /*I*/ 4000, /*W*/ 1000, Bucket::kInsideEarly);
  TEST_ASSERT_FALSE(
      AcceptMatchesTargetBucket(actual, Bucket::kInsideEarly));
}

void test_window_server_id_match() {
  using ae::bench::dw::WindowsMatchServer;
  TEST_ASSERT_TRUE(WindowsMatchServer(7, 7));
  TEST_ASSERT_FALSE(WindowsMatchServer(7, 8));
  TEST_ASSERT_FALSE(WindowsMatchServer(0, 0));
}

void test_duplicate_detection() {
  using ae::bench::dw::Classification;
  using ae::bench::dw::ClassifyInput;
  using ae::bench::dw::ClassifySample;
  ClassifyInput in;
  in.window_open_us = 1;
  in.window_close_us = 2;
  in.server_accept_us = 1;
  in.receive_us = 1;
  in.duplicate_count = 2;
  TEST_ASSERT_EQUAL(static_cast<int>(Classification::kDuplicate),
                    static_cast<int>(ClassifySample(in)));
}

void test_quantile() {
  using ae::bench::dw::Quantile;
  auto v = std::vector<double>{1, 2, 3, 4, 5};
  auto p50 = Quantile(v, 0.5);
  TEST_ASSERT_TRUE(p50.has_value());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, static_cast<float>(*p50));
  auto empty = Quantile({}, 0.5);
  TEST_ASSERT_FALSE(empty.has_value());
}

void test_ipc_frame_roundtrip() {
  using ae::bench::dw::DecodeIpcFrame;
  using ae::bench::dw::EncodeIpcFrame;
  using ae::bench::dw::IpcFrame;
  IpcFrame f{};
  f.type = 15;
  f.side = 1;
  f.event_kind = 3;
  f.run_id_hash = 99;
  f.message_id = 5;
  f.local_steady_us = 123456;
  EncodeIpcFrame(f);
  IpcFrame out{};
  TEST_ASSERT_TRUE(DecodeIpcFrame(&f, sizeof(f), out));
  TEST_ASSERT_EQUAL_UINT32(99, out.run_id_hash);
  TEST_ASSERT_EQUAL_UINT32(5, out.message_id);
  out.crc ^= 1u;
  TEST_ASSERT_FALSE(DecodeIpcFrame(&out, sizeof(out), out));
}

void test_ping_timing_override_default_empty() {
#if AE_ENABLE_PING
  SetPingTimingOverride(std::nullopt);
  TEST_ASSERT_FALSE(GetPingTimingOverride().has_value());
#endif
}

void test_ping_timing_override_separates_fields() {
#if AE_ENABLE_PING
  SetPingTimingOverride(PingTimingOverride{
      std::chrono::milliseconds{4000},
      std::chrono::milliseconds{8000},
      std::chrono::milliseconds{1000},
  });
  auto o = GetPingTimingOverride();
  TEST_ASSERT_TRUE(o.has_value());
  TEST_ASSERT_EQUAL(4000, static_cast<int>(o->actual_interval.count()));
  TEST_ASSERT_EQUAL(8000, static_cast<int>(o->announced_next.count()));
  TEST_ASSERT_EQUAL(1000, static_cast<int>(o->rx_window.count()));
  SetPingTimingOverride(std::nullopt);
#endif
}

void test_pull_messages_wire_id_36() {
  using PullType = decltype(std::declval<AuthorizedApi&>().pull_messages);
  static_assert(MethodId<PullType>::value == 36);
  TEST_ASSERT_EQUAL_UINT8(36, MethodId<PullType>::value);
}

void test_send_message_with_result_wire_id_39() {
  using SendType =
      decltype(std::declval<AuthorizedApi&>().send_message_with_result);
  static_assert(MethodId<SendType>::value == 39);
  TEST_ASSERT_EQUAL_UINT8(39, MethodId<SendType>::value);
}

}  // namespace
}  // namespace ae::test_delivery_window_bench

int test_delivery_window_bench() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_delivery_window_bench::test_bench_message_crc_roundtrip);
  RUN_TEST(ae::test_delivery_window_bench::test_classification_boundaries);
  RUN_TEST(ae::test_delivery_window_bench::test_invalid_accept_bucket);
  RUN_TEST(ae::test_delivery_window_bench::test_window_server_id_match);
  RUN_TEST(ae::test_delivery_window_bench::test_duplicate_detection);
  RUN_TEST(ae::test_delivery_window_bench::test_quantile);
  RUN_TEST(ae::test_delivery_window_bench::test_ipc_frame_roundtrip);
  RUN_TEST(ae::test_delivery_window_bench::test_ping_timing_override_default_empty);
  RUN_TEST(ae::test_delivery_window_bench::test_ping_timing_override_separates_fields);
  RUN_TEST(ae::test_delivery_window_bench::test_pull_messages_wire_id_36);
  RUN_TEST(ae::test_delivery_window_bench::test_send_message_with_result_wire_id_39);
  return UNITY_END();
}
