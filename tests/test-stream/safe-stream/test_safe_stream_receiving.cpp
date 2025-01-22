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

#include "aether/port/tele_init.h"
#include "aether/actions/action_context.h"
#include "aether/api_protocol/protocol_context.h"

#include "aether/stream_api/safe_stream/safe_stream_api.h"
#include "aether/stream_api/safe_stream/safe_stream_receiving.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae::test_safe_stream_receiving {

constexpr char _100_bytes_data[] =
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen "
    "liquor jugs. How vexing...";

constexpr char _200_bytes_data[] =
    "This is a precisely two hundred byte long string created for testing "
    "purposes. It contains random words and phrases to fill up space. The goal "
    "is to reach exactly two hundred bytes without using lore";

constexpr auto config = SafeStreamConfig{
    20 * 1024,
    10 * 1024,
    100,
    2,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{0},
    std::chrono::milliseconds{10},
};

void test_SafeStreamReceiveAFewPackets() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};
  auto pc = ProtocolContext{};

  auto received_packet = DataBuffer{};
  auto confirmed_offset = std::uint16_t{};

  auto receiving = SafeStreamReceivingAction{ac, pc, config};

  auto _0 = receiving.send_data_event().Subscribe([&](auto const& data, auto) {
    auto api_parser = ae::ApiParser(pc, data);
    auto mid = api_parser.Extract<MessageId>();
    switch (mid) {
      case SafeStreamApi::Confirm::kMessageCode: {
        auto confirm = api_parser.Extract<SafeStreamApi::Confirm>();
        confirmed_offset = confirm.offset;
        break;
      }
      default:
        TEST_FAIL_MESSAGE("Unexpected message");
        break;
    }
  });

  auto _1 = receiving.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_packet = std::move(data); });

  ap.Update(epoch);

  receiving.ReceiveSend(
      SafeStreamRingIndex{0},
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)});

  ap.Update(epoch += std::chrono::milliseconds{1});

  TEST_ASSERT_EQUAL(100, received_packet.size());
  TEST_ASSERT_EQUAL(99, confirmed_offset);
  received_packet.clear();

  // duplicate send
  receiving.ReceiveSend(
      SafeStreamRingIndex{0},
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)});

  ap.Update(epoch += std::chrono::milliseconds{1});

  TEST_ASSERT_EQUAL(0, received_packet.size());
  TEST_ASSERT_EQUAL(99, confirmed_offset);

  // send in ordered
  receiving.ReceiveSend(
      SafeStreamRingIndex(confirmed_offset) + 1 + 100,
      {_200_bytes_data + 100, _200_bytes_data + sizeof(_200_bytes_data)});

  ap.Update(epoch += std::chrono::milliseconds{1});

  // not received yet
  // a missed part waiting
  TEST_ASSERT_EQUAL(0, received_packet.size());
  TEST_ASSERT_EQUAL(99, confirmed_offset);

  // add missed part
  receiving.ReceiveSend(SafeStreamRingIndex(confirmed_offset) + 1,
                        {_200_bytes_data, _200_bytes_data + 100});

  ap.Update(epoch += std::chrono::milliseconds{1});

  TEST_ASSERT_EQUAL(200, received_packet.size());
  TEST_ASSERT_EQUAL(299, confirmed_offset);

  received_packet.clear();

  // add late packet
  receiving.ReceiveSend(
      SafeStreamRingIndex(confirmed_offset) + 100,
      {_200_bytes_data, _200_bytes_data + sizeof(_200_bytes_data)});

  ap.Update(epoch += std::chrono::milliseconds{1});

  TEST_ASSERT_EQUAL(0, received_packet.size());
  TEST_ASSERT_EQUAL(299, confirmed_offset);
}

void test_SafeStreamReceiveRequestRepeat() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};
  auto pc = ProtocolContext{};

  auto received_packet = DataBuffer{};
  auto confirmed_offset = std::uint16_t{12};
  auto repeat_requested = std::vector<std::uint16_t>{};

  auto receiving = SafeStreamReceivingAction{ac, pc, config};

  auto _0 = receiving.send_data_event().Subscribe([&](auto const& data, auto) {
    auto api_parser = ae::ApiParser(pc, data);
    auto api = SafeStreamApi{};
    api_parser.Parse(api);
  });

  auto _1 = pc.OnMessage<SafeStreamApi::Confirm>([&](auto const& msg) {
    auto& confirm = msg.message();
    confirmed_offset = confirm.offset;
  });

  auto _2 = pc.OnMessage<SafeStreamApi::RequestRepeat>([&](auto const& msg) {
    auto& request_repeat = msg.message();
    repeat_requested.push_back(request_repeat.offset);
  });

  auto _3 = receiving.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_packet = std::move(data); });

  ap.Update(epoch);

  receiving.ReceiveSend(
      SafeStreamRingIndex{200},
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)});

  ap.Update(epoch += config.send_repeat_timeout);
  TEST_ASSERT(!repeat_requested.empty());
  TEST_ASSERT_EQUAL(0, repeat_requested[0]);
  repeat_requested.clear();

  receiving.ReceiveSend(
      SafeStreamRingIndex{400},
      {_100_bytes_data, _100_bytes_data + sizeof(_100_bytes_data)});

  ap.Update(epoch += config.send_repeat_timeout);
  TEST_ASSERT_EQUAL(2, repeat_requested.size());
  TEST_ASSERT_EQUAL(0, repeat_requested[0]);
  TEST_ASSERT_EQUAL(300, repeat_requested[1]);
  repeat_requested.clear();

  ap.Update(epoch += config.send_repeat_timeout);
  repeat_requested.clear();
  ap.Update(epoch += config.send_repeat_timeout);
  TEST_ASSERT_EQUAL(0, repeat_requested.size());
}

}  // namespace ae::test_safe_stream_receiving

int test_safe_stream_receiving() {
  ae::TeleInit::Init();
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_receiving::test_SafeStreamReceiveAFewPackets);
  RUN_TEST(ae::test_safe_stream_receiving::test_SafeStreamReceiveRequestRepeat);
  return UNITY_END();
}
