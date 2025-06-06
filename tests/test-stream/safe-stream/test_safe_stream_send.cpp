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

#include <optional>

#include "aether/config.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/safe_stream_send_action.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_safe_stream_send {
constexpr auto kTick = std::chrono::milliseconds{1};

constexpr auto config = SafeStreamConfig{
    20 * 1024,
    1056,
    200,  // Increased for longer creative strings
    3,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{25},
    std::chrono::milliseconds{80},
};

class MockSendDataPush final : public ISendDataPush {
 public:
  struct SendData {
    DataChunk data_chunk;
    std::uint16_t repeat_count;
  };

  class MockStreamWriteAction final : public StreamWriteAction {
   public:
    explicit MockStreamWriteAction(ActionContext ac) : StreamWriteAction{ac} {
      state_ = State::kDone;
    }

    TimePoint Update(TimePoint current_time) override {
      Action::Result(*this);
      return current_time;
    }

    void Stop() override {};
  };

  explicit MockSendDataPush(ActionContext ac) : write_list{ac} {}

  ActionView<StreamWriteAction> PushData(DataChunk &&data_chunk,
                                         std::uint16_t repeat_count) override {
    send_data = SendData{std::move(data_chunk), repeat_count};
    return write_list.Emplace();
  }

  std::optional<SendData> send_data;
  ActionList<MockStreamWriteAction> write_list;
};

// Creative test data strings for improved tests (under 200 bytes)
static constexpr std::string_view packet_poetry =
    "Packets travel through the digital void, seeking their destination with "
    "hope and determination. "
    "Some arrive quickly, others take scenic routes through routers.";

static constexpr std::string_view retry_humor =
    "Error 418: I'm a teapot trying to send TCP packets. Please try again when "
    "I become a proper network device. "
    "Brewing connectivity...";

static constexpr std::string_view window_wisdom =
    "A sliding window is like life: you can only see so far ahead, but you "
    "must keep moving forward "
    "to see what comes next in the stream.";

static constexpr std::string_view confirmation_comedy =
    "Waiting for confirmation is like waiting for your crush to text back: "
    "anxiety-inducing with exponential backoff. "
    "Still no response...";

static constexpr std::string_view network_philosophy =
    "In the beginning was the Word, and the Word was 'Hello World', and the "
    "Word was with packets, "
    "and packets were good for carrying data.";

static constexpr std::string_view buffer_ballad =
    "Oh buffer, my buffer, where data goes to wait in queues and stacks they "
    "congregate, "
    "until the network sets them free to travel to destiny.";

// Helper function to create data of specific size
static auto CreateTestData(std::size_t size) {
  std::vector<std::uint8_t> data(size);
  for (std::size_t i = 0; i < size; ++i) {
    data[i] = static_cast<std::uint8_t>(i % 256);
  }
  return data;
}

void test_SendActionCreateAndSend() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{543};

  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, config.max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(SSRingIndex{begin_offset});

  auto data_action = send_action.SendData(ToDataBuffer(packet_poetry));

  bool is_sent = false;
  bool is_error = false;
  data_action->ResultEvent().Subscribe([&](auto const &) { is_sent = true; });
  data_action->ErrorEvent().Subscribe([&](auto const &) { is_error = true; });

  // Verify initial state before update
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
  TEST_ASSERT_FALSE(is_sent);
  TEST_ASSERT_FALSE(is_error);

  ap.Update(Now());

  // Verify data was sent with correct content and offset
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL_STRING_LEN(packet_poetry.data(),
                               send_data_push.send_data->data_chunk.data.data(),
                               packet_poetry.size());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));

  // Verify intermediate state: data sent but not yet confirmed
  TEST_ASSERT_FALSE(is_sent);
  TEST_ASSERT_FALSE(is_error);
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->repeat_count);

  send_action.Confirm(begin_offset +
                      static_cast<SSRingIndex::type>(packet_poetry.size() - 1));
  ap.Update(Now());

  // Verify final state after confirmation
  TEST_ASSERT(is_sent);
  TEST_ASSERT_FALSE(is_error);
}

void test_SendActionRepeatOnTimeout() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{123};
  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, config.max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  auto data_action = send_action.SendData(ToDataBuffer(retry_humor));

  bool is_sent = false;
  bool is_error = false;
  data_action->ResultEvent().Subscribe([&](auto const &) { is_sent = true; });
  data_action->ErrorEvent().Subscribe([&](auto const &) { is_error = true; });

  auto start_time = Now();

  // Initial send
  ap.Update(start_time);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->repeat_count);
  send_data_push.send_data.reset();

  // Wait for timeout and trigger repeat - need two updates: one to detect
  // timeout, one to send
  auto timeout_time = start_time + config.wait_confirm_timeout + kTick;
  ap.Update(timeout_time);
  ap.Update(timeout_time + kTick);

  // Check that repeat was triggered
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->repeat_count);
  TEST_ASSERT_EQUAL_STRING_LEN(retry_humor.data(),
                               send_data_push.send_data->data_chunk.data.data(),
                               retry_humor.size());
  send_data_push.send_data.reset();

  // Confirm after first repeat
  send_action.Confirm(begin_offset +
                      static_cast<SSRingIndex::type>(retry_humor.size() - 1));
  ap.Update(timeout_time + kTick + kTick);

  TEST_ASSERT(is_sent);
  TEST_ASSERT_FALSE(is_error);
}

void test_SendActionErrorOnMaxRepeatExceeded() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{456};
  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, config.max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  auto data_action = send_action.SendData(ToDataBuffer(confirmation_comedy));

  bool is_sent = false;
  bool is_error = false;
  data_action->ResultEvent().Subscribe([&](auto const &) { is_sent = true; });
  data_action->ErrorEvent().Subscribe([&](auto const &) { is_error = true; });

  auto current_time = Now();

  // Initial send (repeat_count = 0)
  ap.Update(current_time);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->repeat_count);
  send_data_push.send_data.reset();

  // Helper lambda for timeout calculation with exponential backoff
  auto wait_confirm_timeout = [&](int repeat_count) {
    auto base_timeout = config.wait_confirm_timeout.count();
    auto factor = (repeat_count > 0)
                      ? (AE_SAFE_STREAM_RTO_GROW_FACTOR * repeat_count)
                      : 1.0;
    return std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(base_timeout * factor)};
  };

  // First repeat (repeat_count = 1)
  current_time += wait_confirm_timeout(0) + kTick;
  ap.Update(current_time);
  ap.Update(current_time + kTick);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->repeat_count);
  send_data_push.send_data.reset();

  // Second repeat (repeat_count = 2)
  current_time += wait_confirm_timeout(1) + kTick;
  ap.Update(current_time);
  ap.Update(current_time + kTick);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(2, send_data_push.send_data->repeat_count);
  send_data_push.send_data.reset();

  // Third repeat attempt should exceed max_repeat_count (3) and trigger error
  // repeat_count = 3, then incremented to 4, which exceeds max_repeat_count (3)
  current_time += wait_confirm_timeout(2) + kTick;
  ap.Update(current_time);
  ap.Update(current_time + kTick);

  // Should not send anymore and trigger error
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
  TEST_ASSERT_FALSE(is_sent);
  TEST_ASSERT(is_error);
}

void test_SendActionRequestRepeat() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{789};
  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, config.max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  auto data_action1 = send_action.SendData(ToDataBuffer(network_philosophy));

  // Initial send
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));
  send_data_push.send_data.reset();

  // Add second data and let it send
  auto data_action2 = send_action.SendData(ToDataBuffer(buffer_ballad));
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto second_offset =
      begin_offset + static_cast<SSRingIndex::type>(network_philosophy.size());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(second_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));
  send_data_push.send_data.reset();

  // Request repeat of first chunk
  send_action.RequestRepeat(begin_offset);
  ap.Update(Now());

  // Should resend from the requested offset
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->repeat_count);
}

void test_SendActionWindowSizeLimit() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{1000};
  auto send_data_push = MockSendDataPush{ac};

  // Use larger packet size for cleaner window size testing
  constexpr std::size_t large_packet_size = 352;
  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, large_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  // Window check is: begin_.Distance(last_sent_ + max_packet_size_) >
  // window_size_ window_size = 1056, max_packet_size = 352 We can send 3
  // packets (1056 bytes) The 4th packet would exceed window
  std::vector<ActionView<SendingDataAction>> data_actions;
  auto packet_data = CreateTestData(large_packet_size);

  // Send 3 packets to fill window exactly (3 * 352 = 1056 bytes)
  for (int i = 0; i < 3; ++i) {
    data_actions.push_back(send_action.SendData(DataBuffer{packet_data}));
    ap.Update(Now());

    // Should send successfully
    TEST_ASSERT(send_data_push.send_data.has_value());
    auto expected_offset =
        begin_offset + static_cast<SSRingIndex::type>(i * large_packet_size);
    TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(expected_offset),
                      static_cast<SSRingIndex::type>(
                          send_data_push.send_data->data_chunk.offset));
    send_data_push.send_data.reset();
  }

  // Try to send another packet that would exceed window size
  // last_sent_ is now at begin_ + 1056, adding max_packet_size (352) = begin_
  // + 1408 begin_.Distance(begin_ + 1408) = 1408 > 1056 (window_size), so
  // should be blocked
  auto blocked_action = send_action.SendData(DataBuffer{packet_data});
  ap.Update(Now());

  // This should NOT send because it would exceed window size
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());

  // Confirm some data to free up window space
  // Confirm first packet (352 bytes)
  auto confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(large_packet_size - 1);
  send_action.Confirm(confirm_offset);
  ap.Update(Now());

  // Now the blocked data should send
  // begin_ moved forward by 352, so window limit is now begin_ + 1056 =
  // old_begin_ + 1408 last_sent_ + max_packet_size = old_begin_ + 1408, which
  // is now within the new window
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto expected_offset =
      begin_offset + static_cast<SSRingIndex::type>(3 * large_packet_size);
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(expected_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));
}

void test_SendActionWindowSizeWithMultipleWaitingPackets() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{2000};
  auto send_data_push = MockSendDataPush{ac};

  // Use larger packet size for cleaner testing
  constexpr std::size_t large_packet_size = 352;
  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, large_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  // Fill the window to its limit
  // window_size = 1056, max_packet_size = 352
  // Send 3 packets to reach exactly 1056 bytes
  std::vector<ActionView<SendingDataAction>> data_actions;
  auto packet_data = CreateTestData(large_packet_size);

  // Send 3 packets of 352 bytes = 1056 bytes (full window)
  for (int i = 0; i < 3; ++i) {
    data_actions.push_back(send_action.SendData(DataBuffer{packet_data}));
    ap.Update(Now());

    TEST_ASSERT(send_data_push.send_data.has_value());
    send_data_push.send_data.reset();
  }

  // Now window should be at limit, add multiple packets that should wait
  // Each attempt to send would make last_sent_ + max_packet_size > begin_ +
  // window_size
  std::vector<ActionView<SendingDataAction>> waiting_actions;
  for (int i = 0; i < 2; ++i) {
    waiting_actions.push_back(send_action.SendData(DataBuffer{packet_data}));
    ap.Update(Now());

    // None of these should send
    TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
  }

  // Confirm some data to free up space (confirm first packet = 352 bytes)
  auto confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(large_packet_size - 1);
  send_action.Confirm(confirm_offset);
  ap.Update(Now());

  // Now first waiting packet should send
  // begin_ moved forward by 352, so window limit increased by 352
  TEST_ASSERT(send_data_push.send_data.has_value());
  send_data_push.send_data.reset();

  // Second waiting packet should not send (window limit reached again)
  // Window now has 2 packets (704 bytes) + new packet (352) = 1056 bytes (full)
  ap.Update(Now());
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
}

void test_SendActionMultipleDataQueueing() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{1337};
  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, config.max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  // Send multiple data chunks rapidly
  auto data_action1 = send_action.SendData(ToDataBuffer(packet_poetry));
  auto data_action2 = send_action.SendData(ToDataBuffer(window_wisdom));

  bool sent1 = false, sent2 = false;
  bool error1 = false, error2 = false;

  data_action1->ResultEvent().Subscribe([&](auto const &) { sent1 = true; });
  data_action1->ErrorEvent().Subscribe([&](auto const &) { error1 = true; });
  data_action2->ResultEvent().Subscribe([&](auto const &) { sent2 = true; });
  data_action2->ErrorEvent().Subscribe([&](auto const &) { error2 = true; });

  // Process queued data
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(begin_offset),
                    static_cast<SSRingIndex::type>(
                        send_data_push.send_data->data_chunk.offset));
  send_data_push.send_data.reset();

  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto second_offset = send_data_push.send_data->data_chunk.offset;
  send_data_push.send_data.reset();

  // Verify proper sequencing (begin_offset should be less than second_offset)
  TEST_ASSERT_GREATER_THAN(static_cast<SSRingIndex::type>(begin_offset),
                           static_cast<SSRingIndex::type>(second_offset));

  // Confirm all data to complete actions
  auto final_offset =
      second_offset + static_cast<SSRingIndex::type>(window_wisdom.size() - 1);
  send_action.Confirm(final_offset);
  ap.Update(Now());

  // Verify actions completed successfully
  TEST_ASSERT(sent1);
  TEST_ASSERT(sent2);
  TEST_ASSERT_FALSE(error1);
  TEST_ASSERT_FALSE(error2);
}

void test_SendDataBiggerThanMaxPacketSize() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{4531};
  constexpr std::uint16_t max_packet_size = 60;
  auto send_data_push = MockSendDataPush{ac};

  auto send_action = SafeStreamSendAction{ac, send_data_push};
  send_action.SetConfig(config.max_repeat_count, max_packet_size,
                        config.window_size, config.wait_confirm_timeout);
  send_action.SetOffset(begin_offset);

  // Send data bigger than max packet size
  auto data_action = send_action.SendData(ToDataBuffer(packet_poetry));
  constexpr auto packets_count = packet_poetry.size() / max_packet_size + 1;
  bool sent = false;
  bool error = false;

  data_action->ResultEvent().Subscribe([&](auto const &) { sent = true; });
  data_action->ErrorEvent().Subscribe([&](auto const &) { error = true; });

  // Process packets
  for (std::uint16_t i = 0; i < packets_count - 1; ++i) {
    auto expected_packet =
        packet_poetry.substr(static_cast<std::size_t>(i * max_packet_size),
                             static_cast<std::size_t>(max_packet_size));

    ap.Update(Now());

    TEST_ASSERT(send_data_push.send_data.has_value());
    TEST_ASSERT_EQUAL(expected_packet.size(),
                      send_data_push.send_data->data_chunk.data.size());
    TEST_ASSERT_EQUAL(
        static_cast<SSRingIndex::type>(
            begin_offset + static_cast<SSRingIndex::type>(i * max_packet_size)),
        static_cast<SSRingIndex::type>(
            send_data_push.send_data->data_chunk.offset));
    TEST_ASSERT_EQUAL_STRING_LEN(
        expected_packet.data(),
        send_data_push.send_data->data_chunk.data.data(),
        expected_packet.size());

    send_data_push.send_data.reset();
  }

  // handle the last packet
  auto expected_offset = (packets_count - 1) * max_packet_size;
  auto expected_size = packet_poetry.size() - expected_offset;
  auto expected_packet =
      packet_poetry.substr(static_cast<std::size_t>(expected_offset),
                           static_cast<std::size_t>(expected_size));

  ap.Update(Now());

  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(expected_packet.size(),
                    send_data_push.send_data->data_chunk.data.size());

  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(begin_offset + expected_offset),
      static_cast<SSRingIndex::type>(
          send_data_push.send_data->data_chunk.offset));

  TEST_ASSERT_EQUAL_STRING_LEN(expected_packet.data(),
                               send_data_push.send_data->data_chunk.data.data(),
                               expected_packet.size());

  // Confirm all data to complete actions
  auto final_offset = begin_offset + expected_offset + expected_size;
  send_action.Confirm(final_offset);
  ap.Update(Now());

  // Verify actions completed successfully
  TEST_ASSERT(sent);
  TEST_ASSERT_FALSE(error);
}

}  // namespace ae::test_safe_stream_send

int test_safe_stream_send() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_send::test_SendActionCreateAndSend);
  RUN_TEST(ae::test_safe_stream_send::test_SendActionMultipleDataQueueing);
  RUN_TEST(ae::test_safe_stream_send::test_SendActionRepeatOnTimeout);
  RUN_TEST(ae::test_safe_stream_send::test_SendActionErrorOnMaxRepeatExceeded);
  RUN_TEST(ae::test_safe_stream_send::test_SendActionRequestRepeat);
  RUN_TEST(ae::test_safe_stream_send::test_SendActionWindowSizeLimit);
  RUN_TEST(ae::test_safe_stream_send::
               test_SendActionWindowSizeWithMultipleWaitingPackets);
  RUN_TEST(ae::test_safe_stream_send::test_SendDataBiggerThanMaxPacketSize);
  return UNITY_END();
}
