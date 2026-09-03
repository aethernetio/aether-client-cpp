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

/**
 * Wire compatibility for AuthorizedApi methods 4 / 36 against
 * aethernetio/aether origin/main ClientServerApi.adsl.yaml
 * (SHA e6c4bd7470948453fb10272b214c42bd6a2ddf51):
 *   ping id 4 returns void params nextConnectMsDuration, rxWindowMs
 *   openReceiveWindow id 36 returns void params durationMs
 *
 * Java FastMeta packing (DataOut LE): META_COMMAND byte, META_REQUEST_ID
 * int32 LE, then each long as int32 LE low/high — same layout C++ BinaryArchive
 * uses for MessageId + RequestId + uint64. Golden vectors are also emitted by
 * tools/gen_authorized_api_java_wire_vectors.js (DataOut-compatible).
 */

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "aether/api_protocol/api_protocol.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "assert_packet.h"

namespace ae::test_authorized_api_wire {
namespace {

// Golden vectors from tools/gen_authorized_api_java_wire_vectors.js mirroring
// io.aether.utils.dataio.DataOut + FastMeta META_COMMAND / META_REQUEST_ID.
constexpr std::uint8_t kJavaPingWire[] = {
    0x04,                    // META_COMMAND = 4
    0x2a, 0x00, 0x00, 0x00,  // requestId = 42 LE
    0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // nextConnect = 10000
    0x30, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // rxWindow = 30000
};

constexpr std::uint8_t kJavaOpenReceiveWindowWire[] = {
    0x24,                    // META_COMMAND = 36
    0x07, 0x00, 0x00, 0x00,  // requestId = 7 LE
    0x30, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // durationMs = 30000
};

// Former C++ bug: Method<36, void()> emits only command id 36, then ping
// bytes follow in the same LoginStream body.
constexpr std::uint8_t kMalformedVoid36ThenPing[] = {
    0x24,  // void pull_messages — NO request id, NO duration
    0x04, 0x2a, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x30, 0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

DataBuffer PackMessage(MessageId id, auto&& message) {
  ProtocolContext pc;
  DataBuffer buf;
  auto packer = ApiPacker{pc, buf};
  packer.Pack(id, std::forward<decltype(message)>(message));
  return buf;
}

void test_AuthorizedApiPingWireMatchesJavaFastMeta() {
  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ctx = ApiContext{api};
  auto promise = ctx->ping(10000ULL, 30000ULL);
  auto const req = promise.request_id();
  DataBuffer packet = std::move(ctx);
  AssertPacket(packet, MessageId{4}, req, std::uint64_t{10000},
               std::uint64_t{30000});

  auto forced =
      PackMessage(MessageId{4}, GenericMessage{RequestId{42}, std::uint64_t{10000},
                                               std::uint64_t{30000}});
  TEST_ASSERT_EQUAL_UINT(sizeof(kJavaPingWire), forced.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kJavaPingWire, forced.data(), forced.size());
}

void test_OpenReceiveWindowWireMatchesJavaFastMeta() {
  auto forced = PackMessage(
      MessageId{36}, GenericMessage{RequestId{7}, std::uint64_t{30000}});
  AssertPacket(forced, MessageId{36}, RequestId{7}, std::uint64_t{30000});
  TEST_ASSERT_EQUAL_UINT(sizeof(kJavaOpenReceiveWindowWire), forced.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kJavaOpenReceiveWindowWire, forced.data(),
                                forced.size());

  ProtocolContext pc;
  AuthorizedApi api{pc};
  auto ctx = ApiContext{api};
  auto promise = ctx->open_receive_window(30000ULL);
  auto const req = promise.request_id();
  DataBuffer live = std::move(ctx);
  AssertPacket(live, MessageId{36}, req, std::uint64_t{30000});
}

void test_MalformedVoid36BeforePingDesyncsOpenReceiveWindowSchema() {
  // Server openReceiveWindow decoder after command 36 expects:
  //   requestId (int32) + durationMs (int64)
  TEST_ASSERT_EQUAL_UINT8(0x24, kMalformedVoid36ThenPing[0]);
  TEST_ASSERT_EQUAL_UINT8(0x04, kMalformedVoid36ThenPing[1]);

  std::vector<std::uint8_t> after_cmd(
      kMalformedVoid36ThenPing + 1,
      kMalformedVoid36ThenPing + sizeof(kMalformedVoid36ThenPing));
  auto archive = ae::seri::BinaryArchive{
      ae::VectorBuffer<ae::PackedSize>{after_cmd},
  };
  RequestId misread_req{};
  std::uint64_t misread_duration{};
  archive.Load(misread_req);
  archive.Load(misread_duration);

  // Bytes were: 04 2a 00 00 | 00 10 27 00 00 00 00 00 ...
  // requestId LE from 04 2a 00 00 = 0x00002a04 = 10756
  TEST_ASSERT_EQUAL_UINT(10756U, static_cast<std::uint32_t>(misread_req));
  TEST_ASSERT_FALSE(misread_duration == 30000ULL);

  std::vector<std::uint8_t> correct(
      kJavaOpenReceiveWindowWire,
      kJavaOpenReceiveWindowWire + sizeof(kJavaOpenReceiveWindowWire));
  TEST_ASSERT_EQUAL_UINT(1U + 4U + 8U, correct.size());
  TEST_ASSERT_TRUE(sizeof(kMalformedVoid36ThenPing) > correct.size());
  TEST_ASSERT_FALSE(std::equal(correct.begin(), correct.end(),
                               kMalformedVoid36ThenPing,
                               kMalformedVoid36ThenPing + correct.size()));
}

}  // namespace
}  // namespace ae::test_authorized_api_wire

int test_authorized_api_wire() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_authorized_api_wire::test_AuthorizedApiPingWireMatchesJavaFastMeta);
  RUN_TEST(
      ae::test_authorized_api_wire::test_OpenReceiveWindowWireMatchesJavaFastMeta);
  RUN_TEST(ae::test_authorized_api_wire::
               test_MalformedVoid36BeforePingDesyncsOpenReceiveWindowSchema);
  return UNITY_END();
}
