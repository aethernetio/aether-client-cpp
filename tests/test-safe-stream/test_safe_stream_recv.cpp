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

#include <vector>
#include <optional>

#include "aether/config.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/safe_stream/details/safe_stream_recv_action.h"

#include "tests/test-stream/to_data_buffer.h"
#include "tests/test-safe-stream/stream-test-ctx.h"

namespace ae::test_safe_stream_recv {
constexpr auto kTick = std::chrono::milliseconds{5};

// Configuration for most tests
constexpr auto config = SafeStreamConfig{
    2048,  // larger window for recv tests
    150,
    3,
    std::chrono::milliseconds{30},  // send_confirm_timeout
    std::chrono::milliseconds{50},  // wait_confirm_timeout
    std::chrono::milliseconds{80},  // send_repeat_timeout
};

// Configuration for window size tests
constexpr auto window_config = SafeStreamConfig{
    512,  // smaller window for testing limits
    128,
    3,
    std::chrono::milliseconds{30},
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{80},
};

static constexpr std::size_t kCapacity = 20 * 1024;
using Receiver = SafeStreamRecvAction<kCapacity>;
using IndexType = RingIndex<kCapacity>;
using IndexRangeType = RingIndexRange<IndexType>;

class MockSendConfirmRepeat final : public ISendAckRepeat {
 public:
  struct AckData {
    std::uint16_t index;
  };

  struct RepeatRequestData {
    std::uint16_t index;
  };

  void SendAck(std::uint16_t index) override { ack_data = AckData{index}; }

  void SendRepeatRequest(std::uint16_t index) override {
    repeat_request_data = RepeatRequestData{index};
  }

  void Reset() {
    ack_data.reset();
    repeat_request_data.reset();
  }

  std::optional<AckData> ack_data;
  std::optional<RepeatRequestData> repeat_request_data;
};

// Creative test data strings
static constexpr std::string_view quantum_data =
    "Schrödinger's packet exists in a superposition of sent and lost states "
    "until observed by the receiver.";

static constexpr std::string_view network_poetry =
    "In the realm of bits and bytes so bright, "
    "packets dance through ethernet light. "
    "SafeStream ensures they all arrive, "
    "keeping data streams alive.";

static constexpr std::string_view dev_wisdom =
    "There are only two hard things in Computer Science: "
    "cache invalidation, naming things, and off-by-one errors.";

static constexpr std::string_view async_philosophy =
    "To await or not to await, that is the async question: "
    "Whether 'tis nobler to block the thread's execution...";

static constexpr std::string_view protocol_haiku =
    "Packets flow like streams,\n"
    "Through routers they find their way,\n"
    "Data reaches home.";

static constexpr std::string_view bug_report =
    "Issue #404: Reality not found. "
    "Expected: working code. "
    "Actual: existential crisis. "
    "Severity: critical to sanity.";

// Helper function to create DataChunk
static auto CreateDataMessage(std::string_view data, std::uint16_t index) {
  return DataMessage{false, 0, index, ToDataBuffer(data)};
}

void test_RecvActionCreateAndReceive() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{1337};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  // Track received data
  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto message = CreateDataMessage(quantum_data, 0);
  receiver.PushData(begin_offset, message);
  ctx.Update(Now());

  // Should immediately emit the data since it's the expected next chunk
  TEST_ASSERT_EQUAL(1, received_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(quantum_data.data(), received_data[0].data(),
                               quantum_data.size());

  // Should not send confirmation immediately (waits for timeout)
  TEST_ASSERT_FALSE(mock_sender.ack_data.has_value());

  // Wait for confirmation timeout and verify confirmation is sent
  auto timeout_time = Now() + config.send_ack_timeout + kTick;
  ctx.Update(timeout_time);

  // Should send confirmation after timeout
  TEST_ASSERT(mock_sender.ack_data.has_value());
  auto expected_confirm_index =
      begin_offset + static_cast<std::uint16_t>(quantum_data.size()) - 1;
  TEST_ASSERT_EQUAL(static_cast<std::uint16_t>(expected_confirm_index),
                    static_cast<std::uint16_t>(mock_sender.ack_data->index));
}

void test_RecvActionInOrderDataChain() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{2048};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create data chunks using DataChunk structure
  auto message1 = CreateDataMessage(network_poetry, 0);
  auto message2 = CreateDataMessage(dev_wisdom, message1.data.size());
  auto message3 = CreateDataMessage(
      async_philosophy, message1.data.size() + message2.data.size());

  // Send chunks in order
  receiver.PushData(begin_offset, message1);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());

  receiver.PushData(begin_offset, message2);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(2, received_data.size());

  receiver.PushData(begin_offset, message3);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(3, received_data.size());

  // Verify content of received data
  TEST_ASSERT_EQUAL_STRING_LEN(network_poetry.data(), received_data[0].data(),
                               network_poetry.size());
  TEST_ASSERT_EQUAL_STRING_LEN(dev_wisdom.data(), received_data[1].data(),
                               dev_wisdom.size());
  TEST_ASSERT_EQUAL_STRING_LEN(async_philosophy.data(), received_data[2].data(),
                               async_philosophy.size());
}

void test_RecvActionOutOfOrderDataChain() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{4096};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create data chunks using DataChunk structure
  auto message1 = CreateDataMessage(protocol_haiku, 0);
  auto message2 = CreateDataMessage(bug_report, message1.data.size());
  auto message3 = CreateDataMessage(
      quantum_data, message1.data.size() + message2.data.size());

  // Send chunks out of order: 1, 3, 2
  receiver.PushData(begin_offset, message1);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // Chunk 1 emitted immediately

  receiver.PushData(begin_offset, message3);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // Chunk 3 buffered (gap exists)

  receiver.PushData(begin_offset, message2);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(
      2, received_data.size());  // Chunks 2+3 emitted as single combined data

  // Verify the data was emitted in correct order despite out-of-order arrival
  TEST_ASSERT_EQUAL_STRING_LEN(protocol_haiku.data(), received_data[0].data(),
                               protocol_haiku.size());

  // Second emission should contain combined chunks 2+3
  auto expected_combined_size = bug_report.size() + quantum_data.size();
  TEST_ASSERT_EQUAL(expected_combined_size, received_data[1].size());

  // Verify combined data starts with chunk 2 content
  TEST_ASSERT_EQUAL_STRING_LEN(bug_report.data(), received_data[1].data(),
                               bug_report.size());

  // Verify combined data contains chunk 3 content at correct offset
  auto chunk3_offset_in_combined = bug_report.size();
  TEST_ASSERT_EQUAL_STRING_LEN(
      quantum_data.data(), received_data[1].data() + chunk3_offset_in_combined,
      quantum_data.size());
}

void test_RecvActionSendConfirmOnTimeout() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{5555};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Send some data using DataChunk
  auto message = CreateDataMessage(network_poetry, 0);
  receiver.PushData(begin_offset, message);
  ctx.Update(Now());

  // Data should be emitted
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Should not send confirmation immediately
  TEST_ASSERT_FALSE(mock_sender.ack_data.has_value());

  // Wait for confirmation timeout
  auto timeout_time = Now() + config.send_ack_timeout + kTick;
  ctx.Update(timeout_time);

  // Should send confirmation after timeout
  TEST_ASSERT(mock_sender.ack_data.has_value());
  auto expected_confirm_index =
      begin_offset + static_cast<std::uint16_t>(network_poetry.size()) - 1;
  TEST_ASSERT_EQUAL(static_cast<std::uint16_t>(expected_confirm_index),
                    static_cast<std::uint16_t>(mock_sender.ack_data->index));
}

void test_RecvActionRequestRepeatOnMissing() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{6789};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create chunks with a gap
  auto message1 = CreateDataMessage(dev_wisdom, 0);
  auto message3 =
      CreateDataMessage(async_philosophy,
                        message1.data.size() + 100);  // Skip chunk 2

  // Send chunk 1 (will be emitted immediately)
  receiver.PushData(begin_offset, message1);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Send chunk 3 (will be buffered due to gap)
  receiver.PushData(begin_offset, message3);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // Still only chunk 1 emitted

  // Should not send repeat request immediately
  TEST_ASSERT_FALSE(mock_sender.repeat_request_data.has_value());

  // Wait for repeat request timeout
  auto timeout_time = Now() + config.send_repeat_timeout + kTick;
  ctx.Update(timeout_time);

  // Should request repeat for missing chunk 2
  TEST_ASSERT(mock_sender.repeat_request_data.has_value());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(begin_offset + message1.data.size()),
      static_cast<std::uint16_t>(mock_sender.repeat_request_data->index));
}

void test_RecvActionRepeatDataHandling() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{8192};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto message = CreateDataMessage(protocol_haiku, 0);

  // Send original data
  receiver.PushData(begin_offset, message);
  // Send repeat request
  message.repeat_count += 1;
  receiver.PushData(begin_offset, message);

  ctx.Update(Now());
  // should receive only one message
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Reset mock to check for immediate confirmation
  mock_sender.Reset();
  auto timeout_time = Now() + config.send_ack_timeout + kTick;
  ctx.Update(timeout_time);

  // Should have only 1 emitted data (no duplicate emission)
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Should have one acknowledgement for repeated data
  TEST_ASSERT(mock_sender.ack_data.has_value());
  auto expected_confirm_index =
      begin_offset + static_cast<std::uint16_t>(protocol_haiku.size()) - 1;
  TEST_ASSERT_EQUAL(static_cast<std::uint16_t>(expected_confirm_index),
                    static_cast<std::uint16_t>(mock_sender.ack_data->index));
}

void test_RecvActionDuplicateDataHandling() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{8192};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto message = CreateDataMessage(protocol_haiku, 0);

  // Send original and duplicate messages
  receiver.PushData(begin_offset, message);
  receiver.PushData(begin_offset, message);
  ctx.Update(Now());
  // should receive only one message
  TEST_ASSERT_EQUAL(1, received_data.size());

  // wait for ack timeout
  auto timeout_time = Now() + config.send_ack_timeout + 2 * kTick;
  ctx.Update(timeout_time);

  // Should send anknowledgement only for the original message
  TEST_ASSERT_TRUE(mock_sender.ack_data.has_value());
  TEST_ASSERT_EQUAL(begin_offset + protocol_haiku.size() - 1,
                    mock_sender.ack_data->index);
}

void test_RecvActionRepeatOverlappingDataHandling() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{8192};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // two messages: part of bug_report and full bug_report
  auto message1 = DataMessage{
      false, 0, 0,
      ToDataBuffer(std::begin(bug_report), std::begin(bug_report) + 20)};

  auto message2 = DataMessage{
      false, 0, 0, ToDataBuffer(std::begin(bug_report), std::end(bug_report))};

  // Send original data
  receiver.PushData(begin_offset, message1);
  ctx.Update(Now());
  // received the first part of the bug_report
  TEST_ASSERT_EQUAL(1, received_data.size());
  TEST_ASSERT_EQUAL(20, received_data[0].size());

  mock_sender.Reset();

  // Send repeated request with full bug_report
  receiver.PushData(begin_offset, message2);
  ctx.Update(Now() + kTick);
  // received the second part of the bug_report
  TEST_ASSERT_EQUAL(2, received_data.size());
  TEST_ASSERT_EQUAL(bug_report.size() - 20, received_data[1].size());

  TEST_ASSERT_EQUAL_STRING_LEN(bug_report.data(), received_data[0].data(), 20);
  TEST_ASSERT_EQUAL_STRING_LEN(bug_report.data() + 20, received_data[1].data(),
                               bug_report.size() - 20);
}

void test_RecvActionInOrderCombinedDataChain() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{3141};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create multiple small contiguous chunks that should be combined
  auto message1 = CreateDataMessage("Part1:", 0);
  auto message2 = CreateDataMessage("Part2:", message1.data.size());
  auto message3 = CreateDataMessage(
      "Part3-Final", message1.data.size() + message2.data.size());

  // Send all chunks rapidly - they should be combined into single emission
  receiver.PushData(begin_offset, message1);
  receiver.PushData(begin_offset, message2);
  receiver.PushData(begin_offset, message3);
  ctx.Update(Now());

  // Should emit all data as single combined chunk
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Verify combined data contains all parts
  auto expected_combined_size =
      message1.data.size() + message2.data.size() + message3.data.size();
  TEST_ASSERT_EQUAL(expected_combined_size, received_data[0].size());

  // Verify content starts with first chunk
  TEST_ASSERT_EQUAL_STRING_LEN("Part1:", received_data[0].data(), 6);
  // Verify content contains second chunk at correct offset
  TEST_ASSERT_EQUAL_STRING_LEN("Part2:", received_data[0].data() + 6, 6);
  // Verify content ends with third chunk
  TEST_ASSERT_EQUAL_STRING_LEN("Part3-Final", received_data[0].data() + 12, 11);
}

void test_RecvActionWindowSizeLimit() {
  TestContext ctx;

  auto begin_offset = std::uint16_t{7777};
  auto mock_sender = MockSendConfirmRepeat{};

  auto receiver = Receiver{ctx, mock_sender, config};

  std::vector<DataBuffer> received_data;
  auto recv_sub = receiver.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Send first chunk
  auto message1 = CreateDataMessage("First chunk: ", 0);
  receiver.PushData(begin_offset, message1);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // First chunk emitted

  // Create second chunk that fits within window (contiguous)
  auto message2 = CreateDataMessage(bug_report, message1.data.size());

  // Create data that exceeds window size (window_size = 512)
  auto invalid_message3 = CreateDataMessage(quantum_data, 600);

  // Send valid second chunk (within window and contiguous)
  receiver.PushData(begin_offset, message2);
  ctx.Update(Now());
  TEST_ASSERT_EQUAL(2, received_data.size());  // Second chunk emitted

  // Send invalid data (outside window) - should be rejected
  receiver.PushData(begin_offset, invalid_message3);
  ctx.Update(Now());

  // Should still have only 2 emitted chunks (invalid data rejected)
  TEST_ASSERT_EQUAL(2, received_data.size());
}
}  // namespace ae::test_safe_stream_recv

int test_safe_stream_recv() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionCreateAndReceive);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionInOrderDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionOutOfOrderDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionSendConfirmOnTimeout);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionRequestRepeatOnMissing);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionRepeatDataHandling);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionDuplicateDataHandling);
  RUN_TEST(
      ae::test_safe_stream_recv::test_RecvActionRepeatOverlappingDataHandling);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionInOrderCombinedDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionWindowSizeLimit);
  return UNITY_END();
}
