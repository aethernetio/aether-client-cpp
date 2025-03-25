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

#include <vector>
#include <chrono>
#include <iterator>

#include "aether/api_protocol/api_message.h"
#include "aether/port/tele_init.h"

#include "aether/actions/action_context.h"

#include "aether/api_protocol/protocol_context.h"
#include "aether/common.h"

#include "aether/transport/actions/channel_connection_action.h"

#include "aether/stream_api/safe_stream/safe_stream_sending.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"

#include "tests/test-stream/safe-stream/to_data_buffer.h"

namespace ae::test_safe_stream_sending {

constexpr char _100_bytes_data[] =
    "The quick brown fox jumps over the lazy dog. Pack my box with five dozen "
    "liquor jugs. How vexing...";

constexpr char _200_bytes_data[] =
    "This is a precisely two hundred byte long string created for testing "
    "purposes. It contains random words and phrases to fill up space. The goal "
    "is to reach exactly two hundred bytes without using lore";

constexpr char _1183_bytes_data[] =
    "In the heart of the cosmos, where the stars weave their eternal dance, "
    "lies a secret known to few. It is a tale of the Everlight, "
    "a radiant beacon that illuminates the boundless depths of space. For "
    "centuries, scholars and dreamers alike have sought its origin, "
    "chasing myths and whispers carried on the stellar winds. Legends speak of "
    "a time when the Everlight was born, a moment of celestial "
    "harmony that bridged worlds and united realms. Through the void, a song "
    "resonates a melody of creation, infinite and unyielding, "
    "echoing through the corridors of time. The Everlight, they say, holds the "
    "power to transform, to inspire, and to reveal the unseen. "
    "Travelers who have ventured near its glow recount visions of galaxies "
    "forming, of life emerging, and of an unspoken truth that binds "
    "all existence. Yet, the Everlight remains elusive, its brilliance both a "
    "guide and a mystery. To reach it is to embrace the unknown, "
    "to journey through shadows and starlight, to seek not just the "
    "destination but the adventure itself. And so, the story continues, "
    "woven into the fabric of the universe, eternal and unbroken, calling to "
    "those who dare to dream beyond the horizon of the possible.";

constexpr auto config = SafeStreamConfig{
    20 * 1024,
    (10 * 1024) - 1,
    (10 * 1024) - 1,
    2,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{10},
    std::chrono::milliseconds{10},
};

void test_SafeStreamSendingFewChunks() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext(ap);
  auto received_data = DataBuffer{};
  auto received_offset = std::uint16_t{};
  bool received_100 = false;
  bool received_200 = false;

  auto sending = SafeStreamSendingAction(ac, config);
  sending.set_max_data_size(100);
  auto send_sub =
      sending.send_event().Subscribe([&](auto offset, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto repeat_sub =
      sending.repeat_event().Subscribe([&](auto, auto, auto&&, auto) {
        TEST_FAIL_MESSAGE("Unexpected repeat");
      });

  auto send_100 = sending.SendData(ToDataBuffer(_100_bytes_data));
  ap.Update(epoch + std::chrono::milliseconds{1});

  auto _1 =
      send_100->SubscribeOnResult([&](auto const&) { received_100 = true; });

  TEST_ASSERT_EQUAL(100, received_data.size());
  TEST_ASSERT_EQUAL_STRING(_100_bytes_data, received_data.data());
  received_data.clear();

  ap.Update(epoch + std::chrono::milliseconds{3});

  auto send_200 = sending.SendData(ToDataBuffer(_200_bytes_data));

  auto _2 =
      send_200->SubscribeOnResult([&](auto const&) { received_200 = true; });

  ap.Update(epoch + std::chrono::milliseconds{3});
  ap.Update(epoch + std::chrono::milliseconds{4});

  TEST_ASSERT_EQUAL(200, received_data.size());
  TEST_ASSERT_EQUAL_STRING(_200_bytes_data, received_data.data());

  sending.Confirm(SafeStreamRingIndex{
      static_cast<std::uint16_t>(received_offset + received_data.size())});
  ap.Update(epoch + std::chrono::milliseconds{3});

  TEST_ASSERT(received_100);
  TEST_ASSERT(received_200);
}

void test_SafeStreamSendingWaitConfirm() {
  auto epoch = TimePoint::clock::now();

  auto config = test_safe_stream_sending::config;
  config.window_size = 200;

  auto ap = ActionProcessor{};
  auto ac = ActionContext(ap);
  auto received_data = DataBuffer{};
  auto received_offset = std::uint16_t{};

  auto sending = SafeStreamSendingAction{ac, config};
  sending.set_max_data_size(100);

  auto send_sub =
      sending.send_event().Subscribe([&](auto offset, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto repeat_sub =
      sending.repeat_event().Subscribe([&](auto, auto, auto&&, auto) {
        TEST_FAIL_MESSAGE("Unexpected repeat");
      });

  sending.SendData(ToDataBuffer(_100_bytes_data));
  sending.SendData(ToDataBuffer(_200_bytes_data));

  for (auto i = 0; i < 3; ++i) {
    ap.Update(epoch + std::chrono::milliseconds{i});
  }

  TEST_ASSERT_EQUAL(200, received_data.size());
  sending.Confirm(
      SafeStreamRingIndex{static_cast<std::uint16_t>(received_offset + 99)});

  ap.Update(epoch + std::chrono::milliseconds{7});
  TEST_ASSERT_EQUAL(300, received_data.size());
  TEST_ASSERT_EQUAL_STRING(_100_bytes_data, received_data.data());
  TEST_ASSERT_EQUAL_STRING(_200_bytes_data,
                           received_data.data() + sizeof(_100_bytes_data));
}

void test_SafeStreamSendingRepeat() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext(ap);

  auto received_data = DataBuffer{};
  auto received_offset = std::uint16_t{};
  auto sending_error = bool{};

  auto sending = SafeStreamSendingAction{ac, config};
  sending.set_max_data_size(100);

  auto send_sub =
      sending.send_event().Subscribe([&](auto offset, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto repeat_sub = sending.repeat_event().Subscribe(
      [&](auto offset, auto /* repeat_count*/, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto send_action = sending.SendData(ToDataBuffer(_100_bytes_data));

  ap.Update(epoch + std::chrono::milliseconds{1});

  auto _1 =
      send_action->SubscribeOnError([&](auto const&) { sending_error = true; });

  TEST_ASSERT_EQUAL(100, received_data.size());

  ap.Update(
      epoch += std::chrono::milliseconds{
          static_cast<std::uint64_t>(50 * AE_SAFE_STREAM_RTO_GROW_FACTOR) + 2});
  TEST_ASSERT_EQUAL(200, received_data.size());

  ap.Update(
      epoch += std::chrono::milliseconds{
          static_cast<std::uint64_t>(50 * AE_SAFE_STREAM_RTO_GROW_FACTOR * 2) +
          2});
  TEST_ASSERT_EQUAL(300, received_data.size());

  // repeat count exceeded
  ap.Update(
      epoch += std::chrono::milliseconds{
          static_cast<std::uint64_t>(50 * AE_SAFE_STREAM_RTO_GROW_FACTOR * 3) +
          2});
  TEST_ASSERT_EQUAL(300, received_data.size());
  TEST_ASSERT(sending_error);
}

void test_SafeStreamSendingRepeatRequest() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext(ap);
  auto received_data = DataBuffer{};
  auto received_offset = std::uint16_t{};
  auto sending_error = bool{};

  auto sending = SafeStreamSendingAction{ac, config};
  sending.set_max_data_size(100);

  auto send_sub =
      sending.send_event().Subscribe([&](auto offset, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto repeat_sub = sending.repeat_event().Subscribe(
      [&](auto offset, auto /* repeat_count*/, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = static_cast<std::uint16_t>(offset);
      });

  auto send_action = sending.SendData(ToDataBuffer(_100_bytes_data));
  auto _1 =
      send_action->SubscribeOnError([&](auto const&) { sending_error = true; });

  ap.Update(epoch += std::chrono::milliseconds{2});
  TEST_ASSERT_EQUAL(100, received_data.size());

  sending.RequestRepeatSend(SafeStreamRingIndex{received_offset});

  ap.Update(epoch += std::chrono::milliseconds{2});
  TEST_ASSERT_EQUAL(200, received_data.size());

  sending.RequestRepeatSend(SafeStreamRingIndex{received_offset});

  ap.Update(epoch += std::chrono::milliseconds{1});
  TEST_ASSERT_EQUAL(300, received_data.size());

  sending.RequestRepeatSend(SafeStreamRingIndex{received_offset});

  ap.Update(epoch += std::chrono::milliseconds{1});
  TEST_ASSERT_EQUAL(300, received_data.size());
  TEST_ASSERT(sending_error);
}

void test_SafeStreamSendingOverBufferCapacity() {
  auto epoch = TimePoint::clock::now();

  auto ap = ActionProcessor{};
  auto ac = ActionContext(ap);
  auto received_data = DataBuffer{};
  auto received_offset = SafeStreamRingIndex{};
  auto sending_error = bool{};

  auto sending = SafeStreamSendingAction{ac, config};
  sending.set_max_data_size(500);

  auto send_sub =
      sending.send_event().Subscribe([&](auto offset, auto&& data, auto) {
        received_data.insert(std::end(received_data), std::begin(data),
                             std::end(data));
        received_offset = offset;
      });

  for (auto i = 0; i < 15; i++) {
    auto old_received_offset = received_offset;
    for (auto j = 0; j < 5; j++) {
      sending.SendData(ToDataBuffer(_1183_bytes_data));
    }
    // update for each sent data chunk
    for (auto k = 0; k < (5 * ((sizeof(_1183_bytes_data) / 500) + 1)); k++) {
      ap.Update(epoch += std::chrono::milliseconds{1});
    }
    // confirm all received
    sending.Confirm(old_received_offset + received_data.size() - 1);

    // test all data received
    for (auto j = 0; j < 5; j++) {
      auto msg = Format("check i,j {},{}", i, j);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(
          _1183_bytes_data,
          received_data.data() + (sizeof(_1183_bytes_data) * j), msg.c_str());
    }
    received_data.clear();
  }
}

}  // namespace ae::test_safe_stream_sending

int test_safe_stream_sending() {
  ae::TeleInit::Init();

  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_sending::test_SafeStreamSendingFewChunks);
  RUN_TEST(ae::test_safe_stream_sending::test_SafeStreamSendingWaitConfirm);
  RUN_TEST(ae::test_safe_stream_sending::test_SafeStreamSendingRepeat);
  RUN_TEST(ae::test_safe_stream_sending::test_SafeStreamSendingRepeatRequest);
  RUN_TEST(
      ae::test_safe_stream_sending::test_SafeStreamSendingOverBufferCapacity);
  return UNITY_END();
}
