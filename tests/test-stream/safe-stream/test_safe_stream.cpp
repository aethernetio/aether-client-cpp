/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/api_protocol/api_message.h"
#include "aether/port/tele_init.h"

#include "aether/actions/action_context.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream.h"

#include "tests/test-stream/mock_read_gate.h"
#include "tests/test-stream/mock_write_gate.h"

namespace ae::test_safe_stream {
constexpr auto config = SafeStreamConfig{
    20 * 1024,
    10 * 1024,
    100,
    2,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{0},
    std::chrono::milliseconds{10},
};

constexpr char _100_bytes_data[] =
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen "
    "liquor jugs. How vexing...";

constexpr char _200_bytes_data[] =
    "This is a precisely two hundred byte long string created for testing "
    "purposes. It contains random words and phrases to fill up space. The goal "
    "is to reach exactly two hundred bytes without using lore";

void test_SafeStreamWriteFewData() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};

  auto received_packet = DataBuffer{};

  auto read_stream = MockReadStream{};
  auto write_stream = MockWriteGate{ap, std::size_t{100}};

  auto safe_stream = SafeStream{ap, config};

  Tie(read_stream, safe_stream, write_stream);

  auto _0 = write_stream.on_write_event().Subscribe(
      [&](auto data, auto) { write_stream.WriteOut(std::move(data)); });

  auto _1 = read_stream.out_data_event().Subscribe([&](auto data) {
    received_packet.insert(std::end(received_packet), std::begin(data),
                           std::end(data));
  });

  safe_stream.in().Write(
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)}, epoch);

  for (auto i = 0; (i < 10) && received_packet.empty(); ++i) {
    ap.Update(epoch += std::chrono::milliseconds{1});
  }

  TEST_ASSERT_EQUAL(sizeof(_100_bytes_data), received_packet.size());

  received_packet.clear();

  safe_stream.in().Write(
      {_200_bytes_data, _200_bytes_data + sizeof(_200_bytes_data)}, epoch);

  for (auto i = 0;
       (i < 10) && (received_packet.size() < sizeof(_200_bytes_data)); ++i) {
    ap.Update(epoch += std::chrono::milliseconds{1});
  }
  TEST_ASSERT_EQUAL(sizeof(_200_bytes_data), received_packet.size());
}

void test_SafeStreamPacketLoss() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto pc = ProtocolContext{};

  auto received_packet = DataBuffer{};

  auto read_stream = MockReadStream{};
  auto write_stream = MockWriteGate{ap, std::size_t{100}};

  auto safe_stream = SafeStream{ap, config};
  Tie(read_stream, safe_stream, write_stream);

  // loop data to itself
  auto _0 = write_stream.on_write_event().Subscribe([&](auto data, auto) {
    auto api_parser = ApiParser{pc, data};
    auto mid = api_parser.Extract<MessageId>();
    if (mid == SafeStreamApi::Send::kMessageCode) {
      // packet "send" is lost
      return;
    }
    write_stream.WriteOut(std::move(data));
  });

  auto _1 = read_stream.out_data_event().Subscribe([&](auto data) {
    received_packet.insert(std::end(received_packet), std::begin(data),
                           std::end(data));
  });

  safe_stream.in().Write(
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)}, epoch);

  for (auto i = 0; (i < 10) && received_packet.empty(); ++i) {
    ap.Update(epoch += std::chrono::milliseconds{10});
  }

  TEST_ASSERT_EQUAL(sizeof(_100_bytes_data), received_packet.size());

  received_packet.clear();

  safe_stream.in().Write(
      {_200_bytes_data, _200_bytes_data + sizeof(_200_bytes_data)}, epoch);

  for (auto i = 0;
       (i < 10) && (received_packet.size() < sizeof(_200_bytes_data)); ++i) {
    ap.Update(epoch += std::chrono::milliseconds{10});
  }
  TEST_ASSERT_EQUAL(sizeof(_200_bytes_data), received_packet.size());
}

}  // namespace ae::test_safe_stream

int test_safe_stream() {
  ae::TeleInit::Init();

  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream::test_SafeStreamWriteFewData);
  RUN_TEST(ae::test_safe_stream::test_SafeStreamPacketLoss);
  return UNITY_END();
}
