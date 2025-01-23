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

#include <string_view>

#include "aether/port/tele_init.h"

#include "aether/actions/action_context.h"
#include "aether/actions/action_processor.h"

#include "aether/stream_api/safe_stream/send_data_buffer.h"
#include "aether/stream_api/safe_stream/safe_stream_types.h"
#include "aether/stream_api/safe_stream/sending_chunk_list.h"

namespace ae::test_safe_stream_types {
void test_OffsetRange() {
  constexpr SafeStreamRingIndex::type window_size = 100;
  {
    constexpr auto half_window_range = OffsetRange{
        SafeStreamRingIndex{25}, SafeStreamRingIndex{75}, window_size};

    TEST_ASSERT_TRUE(half_window_range.Before(SafeStreamRingIndex{76}));
    TEST_ASSERT_TRUE(half_window_range.After(SafeStreamRingIndex{24}));
    TEST_ASSERT_FALSE(half_window_range.Before(SafeStreamRingIndex{75}));
    TEST_ASSERT_FALSE(half_window_range.After(SafeStreamRingIndex{25}));
    TEST_ASSERT_TRUE(half_window_range.InRange(SafeStreamRingIndex{25}));
    TEST_ASSERT_TRUE(half_window_range.InRange(SafeStreamRingIndex{75}));
    TEST_ASSERT_TRUE(half_window_range.InRange(SafeStreamRingIndex{50}));
    TEST_ASSERT_FALSE(half_window_range.InRange(SafeStreamRingIndex{20}));
    TEST_ASSERT_FALSE(half_window_range.InRange(SafeStreamRingIndex{80}));
  }

  constexpr auto window_range = OffsetRange{
      SafeStreamRingIndex{0}, SafeStreamRingIndex{100}, window_size};
  TEST_ASSERT_TRUE(window_range.InRange(SafeStreamRingIndex{0}));
  TEST_ASSERT_TRUE(window_range.InRange(SafeStreamRingIndex{100}));
  TEST_ASSERT_TRUE(window_range.InRange(SafeStreamRingIndex{10}));
  TEST_ASSERT_FALSE(window_range.InRange(SafeStreamRingIndex{110}));
  TEST_ASSERT_TRUE(window_range.Before(SafeStreamRingIndex{110}));
  TEST_ASSERT_TRUE(window_range.After(SafeStreamRingIndex{110}));
}

void test_SendingChunkList() {
  constexpr SafeStreamRingIndex::type window_size = 100;

  SendingChunkList chunk_list{window_size};

  // add three chunks
  chunk_list.Register(SafeStreamRingIndex{0}, SafeStreamRingIndex{5}, Now());
  chunk_list.Register(SafeStreamRingIndex{6}, SafeStreamRingIndex{10}, Now());
  chunk_list.Register(SafeStreamRingIndex{11}, SafeStreamRingIndex{20}, Now());

  TEST_ASSERT_EQUAL(3, chunk_list.size());
  // merge two chunks
  chunk_list.Register(SafeStreamRingIndex{0}, SafeStreamRingIndex{10}, Now());
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  // merge again
  chunk_list.Register(SafeStreamRingIndex{0}, SafeStreamRingIndex{20}, Now());
  TEST_ASSERT_EQUAL(1, chunk_list.size());
  // register a smaller chunk
  chunk_list.Register(SafeStreamRingIndex{0}, SafeStreamRingIndex{10}, Now());
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  // register a smaller between two chunks
  chunk_list.Register(SafeStreamRingIndex{8}, SafeStreamRingIndex{14}, Now());
  TEST_ASSERT_EQUAL(3, chunk_list.size());
  auto& chunk = chunk_list.front();
  TEST_ASSERT(SafeStreamRingIndex(15) == chunk.begin_offset);
  TEST_ASSERT(SafeStreamRingIndex(20) == chunk.end_offset);
  chunk_list.RemoveUpTo(SafeStreamRingIndex{7});
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  chunk_list.RemoveUpTo(SafeStreamRingIndex{20});
  TEST_ASSERT(chunk_list.empty());
}

void test_SendingChunkListRepeatCount() {
  constexpr SafeStreamRingIndex::type window_size = 100;

  SendingChunkList chunk_list{window_size};
  {
    auto& chunk1 = chunk_list.Register(SafeStreamRingIndex{0},
                                       SafeStreamRingIndex{50}, Now());
    chunk1.repeat_count = 1;

    auto& chunk2 = chunk_list.Register(SafeStreamRingIndex{51},
                                       SafeStreamRingIndex{60}, Now());
    chunk2.repeat_count = 2;
    auto& chunk3 = chunk_list.Register(SafeStreamRingIndex{61},
                                       SafeStreamRingIndex{90}, Now());
    chunk3.repeat_count = 3;
  }
  // re register chunk
  {
    auto& chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(1, chunk.repeat_count);
    auto& chunk1 = chunk_list.Register(SafeStreamRingIndex{0},
                                       SafeStreamRingIndex{50}, Now());
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(2, front_chunk.repeat_count);
  }
  // merge chunks
  {
    auto& chunk1 = chunk_list.Register(SafeStreamRingIndex{0},
                                       SafeStreamRingIndex{60}, Now());
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(3, front_chunk.repeat_count);
  }
  // split chunks
  {
    auto& chunk1 = chunk_list.Register(SafeStreamRingIndex{0},
                                       SafeStreamRingIndex{30}, Now());
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(3, front_chunk.repeat_count);
    auto& chunk2 = chunk_list.Register(SafeStreamRingIndex{31},
                                       SafeStreamRingIndex{60}, Now());
    TEST_ASSERT_NOT_EQUAL(&chunk1, &chunk2);
    TEST_ASSERT_EQUAL(1, chunk2.repeat_count);
    chunk2.repeat_count = 2;
  }
}

constexpr std::string_view test_data = "Pure refreshment in every drop";

struct ActionResult {
  bool confirmed;
  bool rejected;
  bool stopped;
};

void test_SendDataBuffer() {
  constexpr SafeStreamRingIndex::type window_size = 100;
  ActionProcessor action_processor;
  ActionContext action_context{action_processor};

  SendDataBuffer send_data_buffer{action_context, window_size};

  auto a1_res = ActionResult{};
  auto a2_res = ActionResult{};
  auto a3_res = ActionResult{};

  // add some data
  auto a1 = send_data_buffer.AddData(
      SendingData{SafeStreamRingIndex{0},
                  DataBuffer{std::begin(test_data), std::end(test_data)}});

  auto a1_result =
      a1->SubscribeOnResult([&](auto const&) { a1_res.confirmed = true; });
  auto a1_error =
      a1->SubscribeOnError([&](auto const&) { a1_res.rejected = true; });
  auto a1_stop =
      a1->SubscribeOnStop([&](auto const&) { a1_res.stopped = true; });

  auto a2 = send_data_buffer.AddData(
      SendingData{SafeStreamRingIndex{test_data.size()},
                  DataBuffer{std::begin(test_data), std::end(test_data)}});
  auto a2_result =
      a2->SubscribeOnResult([&](auto const&) { a2_res.confirmed = true; });
  auto a2_error =
      a2->SubscribeOnError([&](auto const&) { a2_res.rejected = true; });
  auto a2_stop =
      a2->SubscribeOnStop([&](auto const&) { a2_res.stopped = true; });

  auto a3 = send_data_buffer.AddData(
      SendingData{SafeStreamRingIndex{2 * test_data.size()},
                  DataBuffer{std::begin(test_data), std::end(test_data)}});

  auto a3_result =
      a3->SubscribeOnResult([&](auto const&) { a3_res.confirmed = true; });
  auto a3_error =
      a3->SubscribeOnError([&](auto const&) { a3_res.rejected = true; });
  auto a3_stop =
      a3->SubscribeOnStop([&](auto const&) { a3_res.stopped = true; });

  TEST_ASSERT_EQUAL(test_data.size() * 3, send_data_buffer.size());
  // get a slice
  {
    auto data_slice =
        send_data_buffer.GetSlice(SafeStreamRingIndex{0}, test_data.size());
    TEST_ASSERT(SafeStreamRingIndex(0) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(), data_slice.data.data(),
                                 test_data.size());
  }
  // get a slice of data part
  {
    auto data_slice =
        send_data_buffer.GetSlice(SafeStreamRingIndex{5}, test_data.size() - 5);
    TEST_ASSERT(SafeStreamRingIndex(5) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data() + 5, data_slice.data.data(),
                                 test_data.size() - 5);
  }
  // get a slice other two data parts
  {
    auto data_slice =
        send_data_buffer.GetSlice(SafeStreamRingIndex{0}, test_data.size() * 2);
    TEST_ASSERT(SafeStreamRingIndex(0) == data_slice.offset);
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(), data_slice.data.data(),
                                 test_data.size());
    TEST_ASSERT_EQUAL_CHAR_ARRAY(test_data.data(),
                                 data_slice.data.data() + test_data.size(),
                                 test_data.size());
  }
  action_processor.Update(Now());

  // confirm some
  send_data_buffer.Confirm(SafeStreamRingIndex{5});
  action_processor.Update(Now());
  TEST_ASSERT_FALSE(a1_res.confirmed);

  send_data_buffer.Confirm(SafeStreamRingIndex{test_data.size()});
  action_processor.Update(Now());
  TEST_ASSERT_TRUE(a1_res.confirmed);

  // reject some
  send_data_buffer.Reject(SafeStreamRingIndex{test_data.size() - 1});
  action_processor.Update(Now());
  TEST_ASSERT_FALSE(a1_res.rejected);
  TEST_ASSERT_FALSE(a2_res.rejected);

  send_data_buffer.Reject(SafeStreamRingIndex{test_data.size() + 1});
  action_processor.Update(Now());
  TEST_ASSERT_TRUE(a2_res.rejected);

  // stop some
  send_data_buffer.Stop(SafeStreamRingIndex{(2 * test_data.size()) - 1});
  action_processor.Update(Now());
  TEST_ASSERT_FALSE(a3_res.stopped);

  send_data_buffer.Stop(SafeStreamRingIndex{3 * test_data.size()});
  action_processor.Update(Now());
  TEST_ASSERT_TRUE(a3_res.stopped);
}

}  // namespace ae::test_safe_stream_types

int test_safe_stream_types() {
  ae::TeleInit::Init();

  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_types::test_OffsetRange);
  RUN_TEST(ae::test_safe_stream_types::test_SendingChunkList);
  RUN_TEST(ae::test_safe_stream_types::test_SendingChunkListRepeatCount);
  RUN_TEST(ae::test_safe_stream_types::test_SendDataBuffer);
  return UNITY_END();
}
