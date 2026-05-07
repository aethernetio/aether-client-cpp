/*
 * Copyright 2025 Aethernet Inc.
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

#include <string_view>

#include "aether/actions/action_context.h"
#include "aether/actions/repeatable_task.h"

#include "aether/safe_stream/safe_stream.h"

#include "aether/tele/tele.h"

#include "tests/test-stream/to_data_buffer.h"
#include "tests/test-stream/mock_write_stream.h"

#include "stream-test-ctx.h"
#include "mock_bad_streams.h"

namespace ae::test_safe_stream_reliability {
constexpr auto config = SafeStreamConfig{
    4096,
    200,
    100,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{0},
    std::chrono::milliseconds{10},
};

static constexpr std::size_t kCapacity = 20 * 1024;

static constexpr std::string_view test_data =
    "If I was in World War Two, they'd call me \"Spitfire\"";

TimePoint WaitUntil(TimePoint epoch, TimePoint till_time) {
  epoch = till_time;
  return epoch;
}

void TestSendPackets(TestContext& ctx, SafeStream<kCapacity>& sender,
                     SafeStream<kCapacity>& receiver, int wait_messages) {
  int sent_messages = 0;
  int failed_messages = 0;
  std::size_t received_size = 0;

  // send messages periodically
  auto task = RepeatableTask{
      AeContext{ctx},
      [&]() {
        auto& sent_action = sender.Write(ToDataBuffer(test_data));
        sent_action.status_event().Subscribe([&](auto status) {
          if (status != WriteAction::Status::kSuccess) {
            failed_messages++;
          }
          wait_messages--;
        });
        sent_messages++;
      },
      std::chrono::milliseconds{50}, wait_messages};

  // count received messages
  receiver.out_data_event().Subscribe([&](auto const& d) {
    AE_TELED_DEBUG("Received message size {}", d.size());
    received_size += d.size();
  });

  auto epoch = Now();
  auto tick = std::chrono::milliseconds{1};

  while ((wait_messages > 0)) {
    auto new_time = ctx.Update(epoch);
    epoch = WaitUntil(epoch, new_time) + tick;
  }

  TEST_ASSERT_EQUAL(0, wait_messages);
  TEST_ASSERT_EQUAL(0, failed_messages);
  auto expected_size = sent_messages * test_data.size();
  TEST_ASSERT_EQUAL(expected_size, received_size);
}

void test_SafeStreamLostPackets() {
  TestContext ctx;

  // 20% packet loss
  auto s_packet_loss = LostPacketsStream{ctx, 0.2};
  auto r_packet_loss = LostPacketsStream{ctx, 0.2};
  auto s_mock_stream = MockWriteStream{ctx, 1024};
  auto r_mock_stream = MockWriteStream{ctx, 1024};

  s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream<kCapacity>{ctx, config};
  auto receiver = SafeStream<kCapacity>{ctx, config};

  // Tie streams forward and back
  Tie(sender, s_packet_loss, s_mock_stream);
  Tie(receiver, r_packet_loss, r_mock_stream);

  TestSendPackets(ctx, sender, receiver, 100);
}

void test_SafeStreamPacketsReordered() {
  TestContext ctx;

  // 20% packet loss
  auto s_packet_delay =
      PacketDelayStream{ctx, 0.2, std::chrono::milliseconds{50}};
  auto r_packet_delay =
      PacketDelayStream{ctx, 0.2, std::chrono::milliseconds{50}};
  auto s_mock_stream = MockWriteStream{ctx, 1024};
  auto r_mock_stream = MockWriteStream{ctx, 1024};

  s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream<kCapacity>{ctx, config};
  auto receiver = SafeStream<kCapacity>{ctx, config};

  // Tie streams forward and back
  Tie(sender, s_packet_delay, s_mock_stream);
  Tie(receiver, r_packet_delay, r_mock_stream);

  TestSendPackets(ctx, sender, receiver, 100);
}

void test_SafeStreamPacketsLostAndReordered() {
  TestContext ctx;

  // 20% packet reordered
  auto s_packet_delay =
      PacketDelayStream{ctx, 0.1, std::chrono::milliseconds{50}};
  auto r_packet_delay =
      PacketDelayStream{ctx, 0.1, std::chrono::milliseconds{50}};
  // 20% packet loss
  auto s_packet_loss = LostPacketsStream{ctx, 0.1};
  auto r_packet_loss = LostPacketsStream{ctx, 0.1};

  auto s_mock_stream = MockWriteStream{ctx, 1024};
  auto r_mock_stream = MockWriteStream{ctx, 1024};

  s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream<kCapacity>{ctx, config};
  auto receiver = SafeStream<kCapacity>{ctx, config};

  // Tie streams forward and back
  Tie(sender, s_packet_loss, s_packet_delay, s_mock_stream);
  Tie(receiver, r_packet_loss, r_packet_delay, r_mock_stream);

  TestSendPackets(ctx, sender, receiver, 100);
}

}  // namespace ae::test_safe_stream_reliability

int test_safe_stream_reliability() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_reliability::test_SafeStreamLostPackets);
  RUN_TEST(ae::test_safe_stream_reliability::test_SafeStreamPacketsReordered);
  RUN_TEST(
      ae::test_safe_stream_reliability::test_SafeStreamPacketsLostAndReordered);
  return UNITY_END();
}
