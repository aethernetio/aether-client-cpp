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

#include "aether/tele.h"

#include "tests/test-stream/mock_write_stream.h"
#include "tests/test-stream/to_data_buffer.h"

#include "mock_bad_streams.h"
#include "stream-test-ctx.h"

namespace ae::test_safe_stream_reliability {
constexpr auto config = SafeStreamConfig{
    4096,
    200,
    100,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{0},
    std::chrono::milliseconds{10},
};

static constexpr std::string_view test_data =
    "If I was in World War Two, they'd call me \"Spitfire\"";

TimePoint WaitUntil(TimePoint epoch, TimePoint till_time) {
  epoch = till_time;
  return epoch;
}

void TestSendPackets(TestContext& ctx, SafeStream& sender, SafeStream& receiver,
                     int wait_messages) {
  int sent_messages = 0;
  int failed_messages = 0;
  std::size_t received_size = 0;
  MultiSubscription subscriptions;

  // send messages periodically
  auto task = RepeatableTask{
      AeContext{ctx},
      [&]() {
        auto& sent_action = sender.Write(ToDataBuffer(test_data));
        subscriptions += sent_action.status_event().Subscribe([&](auto status) {
          if (status != WriteAction::Status::kSuccess) {
            failed_messages++;
          }
          wait_messages--;
        });
        sent_messages++;
      },
      std::chrono::milliseconds{50}, wait_messages};

  // count received messages
  subscriptions += receiver.out_data_event().Subscribe([&](auto const& d) {
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
  MultiSubscription subscriptions;

  subscriptions += s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  subscriptions += r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream{ctx, config};
  auto receiver = SafeStream{ctx, config};

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
  MultiSubscription subscriptions;

  subscriptions += s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  subscriptions += r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream{ctx, config};
  auto receiver = SafeStream{ctx, config};

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
  MultiSubscription subscriptions;

  subscriptions += s_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { r_mock_stream.WriteOut(data); });
  subscriptions += r_mock_stream.on_write_event().Subscribe(
      [&](auto&& data) { s_mock_stream.WriteOut(data); });

  auto sender = SafeStream{ctx, config};
  auto receiver = SafeStream{ctx, config};

  // Tie streams forward and back
  Tie(sender, s_packet_loss, s_packet_delay, s_mock_stream);
  Tie(receiver, r_packet_loss, r_packet_delay, r_mock_stream);

  TestSendPackets(ctx, sender, receiver, 100);
}

void test_LargeWindowLimitsUnacknowledgedBytes() {
  TestContext ctx;
  constexpr std::size_t kWindow = 256 * 1024;
  auto large = config;
  large.window_size = kWindow;
  large.buffer_capacity = 700 * 1024;
  large.max_packet_size = 300000;
  large.wait_ack_timeout = std::chrono::seconds{2};
  large.minimum_ack_timeout = large.wait_ack_timeout;

  MockWriteStream outgoing{ctx, 2048};
  MockWriteStream incoming{ctx, 2048};
  SafeStream sender{ctx, large};
  SafeStream receiver{ctx, large};
  Tie(sender, outgoing);
  Tie(receiver, incoming);

  DataBuffer expected(large.max_packet_size);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    expected[i] = static_cast<std::uint8_t>((i * 73u + (i >> 8)) & 0xffu);
  }
  DataBuffer received;
  received.reserve(expected.size());
  DataBuffer held_ack;
  bool hold_ack = true;
  bool acknowledged = false;
  MultiSubscription subscriptions;
  subscriptions += outgoing.on_write_event().Subscribe(
      [&](auto const& data) { incoming.WriteOut(data); });
  subscriptions += incoming.on_write_event().Subscribe([&](auto const& ack) {
    if (hold_ack) {
      held_ack = ack;
    } else {
      outgoing.WriteOut(ack);
    }
  });
  subscriptions += receiver.out_data_event().Subscribe([&](auto const& data) {
    received.insert(received.end(), data.begin(), data.end());
  });
  subscriptions += sender.Write(DataBuffer{expected})
                       .status_event()
                       .Subscribe([&](auto status) {
                         acknowledged = status == WriteAction::Status::kSuccess;
                       });

  auto now = Now();
  auto tick = [&] {
    now += std::chrono::milliseconds{1};
    ctx.Update(now);
  };
  for (unsigned i = 0; i < 600; ++i) {
    tick();
  }
  TEST_ASSERT_EQUAL_UINT(kWindow, received.size());
  TEST_ASSERT_FALSE(acknowledged);
  TEST_ASSERT_FALSE(held_ack.empty());

  hold_ack = false;
  outgoing.WriteOut(held_ack);
  for (unsigned i = 0; i < 600 && !acknowledged; ++i) {
    tick();
  }
  TEST_ASSERT_TRUE(acknowledged);
  TEST_ASSERT_EQUAL_UINT(expected.size(), received.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), received.data(), expected.size());
}

}  // namespace ae::test_safe_stream_reliability

int test_safe_stream_reliability() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_reliability::test_SafeStreamLostPackets);
  RUN_TEST(ae::test_safe_stream_reliability::test_SafeStreamPacketsReordered);
  RUN_TEST(
      ae::test_safe_stream_reliability::test_SafeStreamPacketsLostAndReordered);
  RUN_TEST(ae::test_safe_stream_reliability::
               test_LargeWindowLimitsUnacknowledgedBytes);
  return UNITY_END();
}
