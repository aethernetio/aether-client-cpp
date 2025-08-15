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
    200,
    3,
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{25},
    std::chrono::milliseconds{80},
};

class MockSendDataPush final : public ISendDataPush {
 public:
  struct SendData {
    SSRingIndex begin;
    DataMessage data_message;
  };

  class MockStreamWriteAction final : public StreamWriteAction {
   public:
    explicit MockStreamWriteAction(ActionContext ac) : StreamWriteAction{ac} {
      state_ = State::kDone;
    }

    void Stop() override {};
  };

  explicit MockSendDataPush(ActionContext ac) : action_context_{ac} {}

  ActionPtr<StreamWriteAction> PushData(SSRingIndex begin,
                                        DataMessage &&data_message) override {
    send_data = SendData{begin, std::move(data_message)};
    return ActionPtr<MockStreamWriteAction>{action_context_};
  }

  ActionContext action_context_;
  std::optional<SendData> send_data;
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

  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(config.max_packet_size);

  auto data_action = send_action->SendData(ToDataBuffer(packet_poetry));

  bool is_sent = false;
  bool is_error = false;
  data_action->StatusEvent().Subscribe(
      ActionHandler{OnResult{[&]() { is_sent = true; }},
                    OnError{[&]() { is_error = true; }}});

  // Verify initial state before update
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
  TEST_ASSERT_FALSE(is_sent);
  TEST_ASSERT_FALSE(is_error);

  ap.Update(Now());

  // Verify data was sent with correct content and offset
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL_STRING_LEN(
      packet_poetry.data(), send_data_push.send_data->data_message.data.data(),
      packet_poetry.size());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.delta_offset);
  TEST_ASSERT_EQUAL(true, send_data_push.send_data->data_message.reset());

  // Verify intermediate state: data sent but not yet confirmed
  TEST_ASSERT_FALSE(is_sent);
  TEST_ASSERT_FALSE(is_error);
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.repeat_count());

  send_action->Acknowledge(
      send_data_push.send_data->begin +
      send_data_push.send_data->data_message.delta_offset +
      static_cast<SSRingIndex::type>(packet_poetry.size()));
  ap.Update(Now());

  // Verify final state after confirmation
  TEST_ASSERT(is_sent);
  TEST_ASSERT_FALSE(is_error);
}

void test_SendActionRepeatOnTimeout() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(config.max_packet_size);

  auto data_action = send_action->SendData(ToDataBuffer(retry_humor));

  bool is_sent = false;
  bool is_error = false;
  data_action->StatusEvent().Subscribe(
      ActionHandler{OnResult{[&]() { is_sent = true; }},
                    OnError{[&]() { is_error = true; }}});

  auto start_time = Now();

  // Initial send
  ap.Update(start_time);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.repeat_count());
  send_data_push.send_data.reset();

  // Wait for timeout and trigger repeat - need two updates: one to detect
  // timeout, one to send
  auto timeout_time = start_time + config.wait_ack_timeout + kTick;
  ap.Update(timeout_time);
  ap.Update(timeout_time + kTick);

  // Check that repeat was triggered
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->data_message.repeat_count());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.delta_offset);
  TEST_ASSERT_EQUAL_STRING_LEN(
      retry_humor.data(), send_data_push.send_data->data_message.data.data(),
      retry_humor.size());

  // Confirm after first repeat
  send_action->Acknowledge(send_data_push.send_data->begin +
                           send_data_push.send_data->data_message.delta_offset +
                           +static_cast<SSRingIndex::type>(retry_humor.size()));
  ap.Update(timeout_time + kTick + kTick);

  TEST_ASSERT(is_sent);
  TEST_ASSERT_FALSE(is_error);
}

void test_SendActionErrorOnMaxRepeatExceeded() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(config.max_packet_size);

  auto data_action = send_action->SendData(ToDataBuffer(confirmation_comedy));

  bool is_sent = false;
  bool is_error = false;
  data_action->StatusEvent().Subscribe(
      ActionHandler{OnResult{[&]() { is_sent = true; }},
                    OnError{[&]() { is_error = true; }}});

  auto current_time = Now();

  // Initial send (repeat_count = 0)
  ap.Update(current_time);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.repeat_count());
  send_data_push.send_data.reset();

  // Helper lambda for timeout calculation with exponential backoff
  auto wait_confirm_timeout = [&](int repeat_count) {
    auto base_timeout = config.wait_ack_timeout.count();
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
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->data_message.repeat_count());
  send_data_push.send_data.reset();

  // Second repeat (repeat_count = 2)
  current_time += wait_confirm_timeout(1) + kTick;
  ap.Update(current_time);
  ap.Update(current_time + kTick);
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(2, send_data_push.send_data->data_message.repeat_count());
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

  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(config.max_packet_size);

  auto data_action1 = send_action->SendData(ToDataBuffer(network_philosophy));

  SSRingIndex begin_offset;

  // Initial send
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.delta_offset);
  send_data_push.send_data.reset();

  // Add second data and let it send
  auto data_action2 = send_action->SendData(ToDataBuffer(buffer_ballad));
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(network_philosophy.size(),
                    send_data_push.send_data->data_message.delta_offset);
  begin_offset = send_data_push.send_data->begin;
  send_data_push.send_data.reset();

  // Request repeat of first chunk
  send_action->RequestRepeat(begin_offset);
  ap.Update(Now());

  // Should resend from the requested offset
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.delta_offset);
  TEST_ASSERT_EQUAL(1, send_data_push.send_data->data_message.repeat_count());
}

void test_SendActionWindowSizeLimit() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto send_data_push = MockSendDataPush{ac};

  // Use larger packet size for cleaner window size testing
  constexpr std::size_t large_packet_size = 352;
  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(large_packet_size);

  // Window check is: begin_.Distance(last_sent_ + max_packet_size_) >
  // window_size_ window_size = 1056, max_packet_size = 352 We can send 3
  // packets (1056 bytes) The 4th packet would exceed window
  auto packet_data = CreateTestData(large_packet_size);
  SSRingIndex begin_offset;

  // Send 3 packets to fill window exactly (3 * 352 = 1056 bytes)
  for (int i = 0; i < 3; ++i) {
    send_action->SendData(DataBuffer{packet_data});
    ap.Update(Now());

    // Should send successfully
    TEST_ASSERT(send_data_push.send_data.has_value());
    auto expected_delta = i * large_packet_size;
    TEST_ASSERT_EQUAL(expected_delta,
                      send_data_push.send_data->data_message.delta_offset);
    begin_offset = send_data_push.send_data->begin;
    send_data_push.send_data.reset();
  }

  // Try to send another packet that would exceed window size
  // last_sent_ is now at begin_ + 1056, adding max_packet_size (352) = begin_
  // + 1408 begin_.Distance(begin_ + 1408) = 1408 > 1056 (window_size), so
  // should be blocked
  send_action->SendData(DataBuffer{packet_data});
  ap.Update(Now());

  // This should NOT send because it would exceed window size
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());

  // Confirm some data to free up window space
  // Confirm first packet (352 bytes)
  auto confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(large_packet_size);
  send_action->Acknowledge(confirm_offset);
  ap.Update(Now());

  // Now the blocked data should send
  // begin moved forward by 352, so the next packet delta should be 1056 - 352 =
  // 704
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto expected_delta = 2 * large_packet_size;
  TEST_ASSERT_EQUAL(expected_delta,
                    send_data_push.send_data->data_message.delta_offset);
}

void test_SendActionWindowSizeWithMultipleWaitingPackets() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto send_data_push = MockSendDataPush{ac};

  // Use larger packet size for cleaner testing
  constexpr std::size_t large_packet_size = 352;
  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(large_packet_size);

  // Fill the window to its limit
  // window_size = 1056, max_packet_size = 352
  // Send 3 packets to reach exactly 1056 bytes
  auto packet_data = CreateTestData(large_packet_size);
  SSRingIndex begin_offset;

  // Send 3 packets of 352 bytes = 1056 bytes (full window)
  for (int i = 0; i < 3; ++i) {
    send_action->SendData(DataBuffer{packet_data});
    ap.Update(Now());

    TEST_ASSERT(send_data_push.send_data.has_value());
    auto expected_delta = i * large_packet_size;
    TEST_ASSERT_EQUAL(expected_delta,
                      send_data_push.send_data->data_message.delta_offset);
    begin_offset = send_data_push.send_data->begin;
    send_data_push.send_data.reset();
  }

  // Now window should be at limit, add multiple packets that should wait
  // Each attempt to send would make last_sent_ + max_packet_size > begin_ +
  // window_size
  for (int i = 0; i < 2; ++i) {
    send_action->SendData(DataBuffer{packet_data});
    ap.Update(Now());

    // None of these should send
    TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
  }

  // Confirm some data to free up space (confirm first packet = 352 bytes)
  auto confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(large_packet_size);
  send_action->Acknowledge(confirm_offset);
  ap.Update(Now());

  // Now first waiting packet should send
  // begin moved forward by 352, so the next packet delta should be 1056 - 352 =
  // 704
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto expected_delta = 2 * large_packet_size;
  TEST_ASSERT_EQUAL(expected_delta,
                    send_data_push.send_data->data_message.delta_offset);
  send_data_push.send_data.reset();

  // Second waiting packet should not send (window limit reached again)
  // Window now has 2 packets (704 bytes) + new packet (352) = 1056 bytes (full)
  ap.Update(Now());
  TEST_ASSERT_FALSE(send_data_push.send_data.has_value());
}

void test_SendActionMultipleDataQueueing() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(config.max_packet_size);

  // Send multiple data chunks rapidly
  auto data_action1 = send_action->SendData(ToDataBuffer(packet_poetry));
  auto data_action2 = send_action->SendData(ToDataBuffer(window_wisdom));

  SSRingIndex begin_offset;
  bool sent1 = false;
  bool error1 = false;
  bool sent2 = false;
  bool error2 = false;

  data_action1->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { sent1 = true; }}, OnError{[&]() { error1 = true; }}});

  data_action2->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { sent2 = true; }}, OnError{[&]() { error2 = true; }}});

  // Process queued data
  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  TEST_ASSERT_EQUAL(0, send_data_push.send_data->data_message.delta_offset);
  begin_offset = send_data_push.send_data->begin;
  send_data_push.send_data.reset();

  ap.Update(Now());
  TEST_ASSERT(send_data_push.send_data.has_value());
  auto second_delta = send_data_push.send_data->data_message.delta_offset;
  send_data_push.send_data.reset();

  // Verify proper sequencing (begin_offset should be less than second_offset)
  TEST_ASSERT_GREATER_THAN(0, static_cast<SSRingIndex::type>(second_delta));

  // Confirm all data to complete actions
  auto final_offset = begin_offset + second_delta +
                      static_cast<SSRingIndex::type>(window_wisdom.size());
  send_action->Acknowledge(final_offset);
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

  constexpr std::uint16_t max_packet_size = 60;
  auto send_data_push = MockSendDataPush{ac};

  auto send_action =
      ActionPtr<SafeStreamSendAction>{ac, send_data_push, config};
  send_action->SetMaxPayload(max_packet_size);

  // Send data bigger than max packet size
  auto data_action = send_action->SendData(ToDataBuffer(packet_poetry));
  constexpr auto packets_count = packet_poetry.size() / max_packet_size + 1;
  bool sent = false;
  bool error = false;

  data_action->StatusEvent().Subscribe(
      ActionHandler{OnResult{[&]() { sent = true; }},
                    OnError{[&](auto const &) { error = true; }}});

  // Process packets
  for (std::uint16_t i = 0; i < packets_count - 1; ++i) {
    auto expected_packet =
        packet_poetry.substr(static_cast<std::size_t>(i * max_packet_size),
                             static_cast<std::size_t>(max_packet_size));

    ap.Update(Now());

    TEST_ASSERT(send_data_push.send_data.has_value());
    TEST_ASSERT_EQUAL(expected_packet.size(),
                      send_data_push.send_data->data_message.data.size());
    TEST_ASSERT_EQUAL(i * max_packet_size,
                      send_data_push.send_data->data_message.delta_offset);
    TEST_ASSERT_EQUAL_STRING_LEN(
        expected_packet.data(),
        send_data_push.send_data->data_message.data.data(),
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
                    send_data_push.send_data->data_message.data.size());

  TEST_ASSERT_EQUAL(expected_offset,
                    send_data_push.send_data->data_message.delta_offset);

  TEST_ASSERT_EQUAL_STRING_LEN(
      expected_packet.data(),
      send_data_push.send_data->data_message.data.data(),
      expected_packet.size());

  // Confirm all data to complete actions
  auto final_offset =
      send_data_push.send_data->begin + expected_offset + expected_size;
  send_action->Acknowledge(final_offset);
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
