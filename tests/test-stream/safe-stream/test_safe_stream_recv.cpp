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
#include <vector>

#include "aether/config.h"
#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"
#include "aether/stream_api/safe_stream/safe_stream_config.h"
#include "aether/stream_api/safe_stream/safe_stream_recv_action.h"
#include "aether/stream_api/safe_stream/send_data_buffer.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_safe_stream_recv {
constexpr auto kTick = std::chrono::milliseconds{1};

// Configuration for most tests
constexpr auto config = SafeStreamConfig{
    20 * 1024,
    2048,  // larger window for recv tests
    150,
    3,
    std::chrono::milliseconds{30},  // send_confirm_timeout
    std::chrono::milliseconds{50},  // wait_confirm_timeout
    std::chrono::milliseconds{80},  // send_repeat_timeout
};

// Configuration for window size tests
constexpr auto window_config = SafeStreamConfig{
    20 * 1024,
    512,  // smaller window for testing limits
    128,
    3,
    std::chrono::milliseconds{30},
    std::chrono::milliseconds{50},
    std::chrono::milliseconds{80},
};

class MockSendConfirmRepeat final : public ISendConfirmRepeat {
 public:
  struct ConfirmData {
    SSRingIndex offset;
  };

  struct RepeatRequestData {
    SSRingIndex offset;
  };

  void SendConfirm(SSRingIndex offset) override {
    confirm_data = ConfirmData{offset};
  }

  void SendRepeatRequest(SSRingIndex offset) override {
    repeat_request_data = RepeatRequestData{offset};
  }

  void Reset() {
    confirm_data.reset();
    repeat_request_data.reset();
  }

  std::optional<ConfirmData> confirm_data;
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
static auto CreateDataChunk(std::string_view data, SSRingIndex offset) {
  return DataChunk{ToDataBuffer(data), offset};
}

void test_RecvActionCreateAndReceive() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{1337};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  // Track received data
  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto start_time = Now();

  // Send a single chunk using DataChunk structure
  auto chunk = CreateDataChunk(quantum_data, begin_offset);
  recv_action.PushData(DataBuffer{chunk.data}, chunk.offset, 0);
  ap.Update(start_time);

  // Should immediately emit the data since it's the expected next chunk
  TEST_ASSERT_EQUAL(1, received_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(quantum_data.data(), received_data[0].data(),
                               quantum_data.size());

  // Should not send confirmation immediately (waits for timeout)
  TEST_ASSERT_FALSE(mock_sender.confirm_data.has_value());

  // Wait for confirmation timeout and verify confirmation is sent
  auto timeout_time = start_time + config.send_confirm_timeout + kTick;
  ap.Update(timeout_time);

  // Should send confirmation after timeout
  TEST_ASSERT(mock_sender.confirm_data.has_value());
  auto expected_confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(quantum_data.size() - 1);
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(expected_confirm_offset),
      static_cast<SSRingIndex::type>(mock_sender.confirm_data->offset));
}

void test_RecvActionInOrderDataChain() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{2048};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create data chunks using DataChunk structure
  auto chunk1 = CreateDataChunk(network_poetry, begin_offset);
  auto chunk2 = CreateDataChunk(
      dev_wisdom,
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size()));
  auto chunk3 = CreateDataChunk(
      async_philosophy,
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size() +
                                                    chunk2.data.size()));

  // Send chunks in order
  recv_action.PushData(DataBuffer{chunk1.data}, chunk1.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());

  recv_action.PushData(DataBuffer{chunk2.data}, chunk2.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(2, received_data.size());

  recv_action.PushData(DataBuffer{chunk3.data}, chunk3.offset, 0);
  ap.Update(Now());
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
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{4096};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create data chunks using DataChunk structure
  auto chunk1 = CreateDataChunk(protocol_haiku, begin_offset);
  auto chunk2 = CreateDataChunk(
      bug_report,
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size()));
  auto chunk3 = CreateDataChunk(
      quantum_data,
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size() +
                                                    chunk2.data.size()));

  // Send chunks out of order: 1, 3, 2
  recv_action.PushData(DataBuffer{chunk1.data}, chunk1.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // Chunk 1 emitted immediately

  recv_action.PushData(DataBuffer{chunk3.data}, chunk3.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // Chunk 3 buffered (gap exists)

  recv_action.PushData(DataBuffer{chunk2.data}, chunk2.offset, 0);
  ap.Update(Now());
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
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{5555};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto start_time = Now();

  // Send some data using DataChunk
  auto chunk = CreateDataChunk(network_poetry, begin_offset);
  recv_action.PushData(DataBuffer{chunk.data}, chunk.offset, 0);
  ap.Update(start_time);

  // Data should be emitted
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Should not send confirmation immediately
  TEST_ASSERT_FALSE(mock_sender.confirm_data.has_value());

  // Wait for confirmation timeout
  auto timeout_time = start_time + config.send_confirm_timeout + kTick;
  ap.Update(timeout_time);

  // Should send confirmation after timeout
  TEST_ASSERT(mock_sender.confirm_data.has_value());
  auto expected_confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(network_poetry.size() - 1);
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(expected_confirm_offset),
      static_cast<SSRingIndex::type>(mock_sender.confirm_data->offset));
}

void test_RecvActionRequestRepeatOnMissing() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{6789};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create chunks with a gap
  auto chunk1 = CreateDataChunk(dev_wisdom, begin_offset);
  auto missing_chunk2_offset =
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size());
  auto chunk3 =
      CreateDataChunk(async_philosophy,
                      missing_chunk2_offset +
                          static_cast<SSRingIndex::type>(100));  // Skip chunk 2

  auto start_time = Now();

  // Send chunk 1 (will be emitted immediately)
  recv_action.PushData(DataBuffer{chunk1.data}, chunk1.offset, 0);
  ap.Update(start_time);
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Send chunk 3 (will be buffered due to gap)
  recv_action.PushData(DataBuffer{chunk3.data}, chunk3.offset, 0);
  ap.Update(start_time);
  TEST_ASSERT_EQUAL(1, received_data.size());  // Still only chunk 1 emitted

  // Should not send repeat request immediately
  TEST_ASSERT_FALSE(mock_sender.repeat_request_data.has_value());

  // Wait for repeat request timeout
  auto timeout_time = start_time + config.send_repeat_timeout + kTick;
  ap.Update(timeout_time);

  // Should request repeat for missing chunk 2
  TEST_ASSERT(mock_sender.repeat_request_data.has_value());
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(missing_chunk2_offset),
      static_cast<SSRingIndex::type>(mock_sender.repeat_request_data->offset));
}

void test_RecvActionDuplicateDataHandling() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{8192};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  auto chunk = CreateDataChunk(protocol_haiku, begin_offset);

  // Send original data
  recv_action.PushData(DataBuffer{chunk.data}, chunk.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Reset mock to check for immediate confirmation
  mock_sender.Reset();

  // Send duplicate with higher repeat count
  recv_action.PushData(DataBuffer{chunk.data}, chunk.offset, 1);
  ap.Update(Now());

  // Should still have only 1 emitted data (no duplicate emission)
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Should send immediate confirmation for duplicate
  TEST_ASSERT(mock_sender.confirm_data.has_value());
  auto expected_confirm_offset =
      begin_offset + static_cast<SSRingIndex::type>(protocol_haiku.size() - 1);
  TEST_ASSERT_EQUAL(
      static_cast<SSRingIndex::type>(expected_confirm_offset),
      static_cast<SSRingIndex::type>(mock_sender.confirm_data->offset));
}

void test_RecvActionInOrderCombinedDataChain() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{3141};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Create multiple small contiguous chunks that should be combined
  auto chunk1 = CreateDataChunk("Part1:", begin_offset);
  auto chunk2 = CreateDataChunk(
      "Part2:",
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size()));
  auto chunk3 = CreateDataChunk(
      "Part3-Final",
      begin_offset + static_cast<SSRingIndex::type>(chunk1.data.size() +
                                                    chunk2.data.size()));

  // Send all chunks rapidly - they should be combined into single emission
  recv_action.PushData(DataBuffer{chunk1.data}, chunk1.offset, 0);
  recv_action.PushData(DataBuffer{chunk2.data}, chunk2.offset, 0);
  recv_action.PushData(DataBuffer{chunk3.data}, chunk3.offset, 0);
  ap.Update(Now());

  // Should emit all data as single combined chunk
  TEST_ASSERT_EQUAL(1, received_data.size());

  // Verify combined data contains all parts
  auto expected_combined_size =
      chunk1.data.size() + chunk2.data.size() + chunk3.data.size();
  TEST_ASSERT_EQUAL(expected_combined_size, received_data[0].size());

  // Verify content starts with first chunk
  TEST_ASSERT_EQUAL_STRING_LEN("Part1:", received_data[0].data(), 6);
  // Verify content contains second chunk at correct offset
  TEST_ASSERT_EQUAL_STRING_LEN("Part2:", received_data[0].data() + 6, 6);
  // Verify content ends with third chunk
  TEST_ASSERT_EQUAL_STRING_LEN("Part3-Final", received_data[0].data() + 12, 11);
}

void test_RecvActionWindowSizeLimit() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto begin_offset = SSRingIndex{7777};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(window_config.window_size,
                        window_config.send_confirm_timeout,
                        window_config.send_repeat_timeout);
  recv_action.SetOffset(begin_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Send first chunk
  auto first_chunk = CreateDataChunk("First chunk: ", begin_offset);
  recv_action.PushData(DataBuffer{first_chunk.data}, first_chunk.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, received_data.size());  // First chunk emitted

  // Create second chunk that fits within window (contiguous)
  auto second_chunk = CreateDataChunk(
      bug_report,
      begin_offset + static_cast<SSRingIndex::type>(first_chunk.data.size()));

  // Create data that exceeds window size (window_size = 512)
  auto invalid_chunk = CreateDataChunk(
      quantum_data, begin_offset + static_cast<SSRingIndex::type>(600));

  // Send valid second chunk (within window and contiguous)
  recv_action.PushData(DataBuffer{second_chunk.data}, second_chunk.offset, 0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(2, received_data.size());  // Second chunk emitted

  // Send invalid data (outside window) - should be rejected
  recv_action.PushData(DataBuffer{invalid_chunk.data}, invalid_chunk.offset, 0);
  ap.Update(Now());

  // Should still have only 2 emitted chunks (invalid data rejected)
  TEST_ASSERT_EQUAL(2, received_data.size());
}

void test_RecvActionSetOffsetAndConfig() {
  auto ap = ActionProcessor{};
  auto ac = ActionContext{ap};

  auto initial_offset = SSRingIndex{9876};
  auto mock_sender = MockSendConfirmRepeat{};

  auto recv_action = SafeStreamRecvAction{ac, mock_sender};
  recv_action.SetConfig(config.window_size, config.send_confirm_timeout,
                        config.send_repeat_timeout);
  recv_action.SetOffset(initial_offset);

  std::vector<DataBuffer> received_data;
  auto recv_sub = recv_action.receive_event().Subscribe(
      [&](DataBuffer&& data) { received_data.push_back(std::move(data)); });

  // Add some data to buffer using DataChunk
  auto buffered_chunk = CreateDataChunk(
      network_poetry, initial_offset + static_cast<SSRingIndex::type>(100));
  recv_action.PushData(DataBuffer{buffered_chunk.data}, buffered_chunk.offset,
                       0);
  ap.Update(Now());
  TEST_ASSERT_EQUAL(0, received_data.size());  // Buffered (out of order)

  // Reset offset - should clear all buffered data
  auto new_offset = SSRingIndex{12345};
  recv_action.SetOffset(new_offset);

  // Send new data at new offset using DataChunk
  auto new_chunk = CreateDataChunk(dev_wisdom, new_offset);
  recv_action.PushData(DataBuffer{new_chunk.data}, new_chunk.offset, 0);
  ap.Update(Now());

  // Should emit new data immediately (old buffered data cleared)
  TEST_ASSERT_EQUAL(1, received_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(dev_wisdom.data(), received_data[0].data(),
                               dev_wisdom.size());

  // Test config changes
  auto new_confirm_timeout = std::chrono::milliseconds{100};
  recv_action.SetConfig(config.window_size, new_confirm_timeout,
                        config.send_repeat_timeout);

  // The new config should take effect for future operations
  // (This is primarily testing that SetConfig doesn't crash and accepts new
  // values)
}

}  // namespace ae::test_safe_stream_recv

int test_safe_stream_recv() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionCreateAndReceive);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionInOrderDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionInOrderCombinedDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionOutOfOrderDataChain);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionSendConfirmOnTimeout);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionRequestRepeatOnMissing);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionDuplicateDataHandling);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionWindowSizeLimit);
  RUN_TEST(ae::test_safe_stream_recv::test_RecvActionSetOffsetAndConfig);
  return UNITY_END();
}
