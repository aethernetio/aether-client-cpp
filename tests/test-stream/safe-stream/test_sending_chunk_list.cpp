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

#include "aether/stream_api/safe_stream/sending_chunk_list.h"

namespace ae::test_sending_chunk_list {

void test_SendingChunkList() {
  constexpr auto begin = SSRingIndex{0};

  SendingChunkList chunk_list{};

  // add three chunks
  chunk_list.Register(SSRingIndex{0}, SSRingIndex{5}, Now(), begin);
  chunk_list.Register(SSRingIndex{6}, SSRingIndex{10}, Now(), begin);
  chunk_list.Register(SSRingIndex{11}, SSRingIndex{20}, Now(), begin);

  TEST_ASSERT_EQUAL(3, chunk_list.size());
  // merge two chunks
  chunk_list.Register(SSRingIndex{0}, SSRingIndex{10}, Now(), begin);
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  // merge again
  chunk_list.Register(SSRingIndex{0}, SSRingIndex{20}, Now(), begin);
  TEST_ASSERT_EQUAL(1, chunk_list.size());
  // register a smaller chunk
  chunk_list.Register(SSRingIndex{0}, SSRingIndex{10}, Now(), begin);
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  // register a smaller between two chunks
  chunk_list.Register(SSRingIndex{8}, SSRingIndex{14}, Now(), begin);
  TEST_ASSERT_EQUAL(3, chunk_list.size());
  auto& chunk = chunk_list.front();
  TEST_ASSERT(SSRingIndex{15}(begin) == chunk.begin_offset);
  TEST_ASSERT(SSRingIndex{20}(begin) == chunk.end_offset);
  chunk_list.RemoveUpTo(SSRingIndex{7}, begin);
  TEST_ASSERT_EQUAL(2, chunk_list.size());
  chunk_list.RemoveUpTo(SSRingIndex{20}, begin);
  TEST_ASSERT(chunk_list.empty());
}

void test_SendingChunkListRepeatCount() {
  constexpr auto begin = SSRingIndex{0};

  SendingChunkList chunk_list{};
  {
    auto& chunk1 =
        chunk_list.Register(SSRingIndex{0}, SSRingIndex{50}, Now(), begin);
    chunk1.repeat_count = 1;

    auto& chunk2 =
        chunk_list.Register(SSRingIndex{51}, SSRingIndex{60}, Now(), begin);
    chunk2.repeat_count = 2;
    auto& chunk3 =
        chunk_list.Register(SSRingIndex{61}, SSRingIndex{90}, Now(), begin);
    chunk3.repeat_count = 3;
  }
  // re register chunk
  {
    auto& chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(1, chunk.repeat_count);
    auto& chunk1 =
        chunk_list.Register(SSRingIndex{0}, SSRingIndex{50}, Now(), begin);
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(2, front_chunk.repeat_count);
  }
  // merge chunks
  {
    auto& chunk1 =
        chunk_list.Register(SSRingIndex{0}, SSRingIndex{60}, Now(), begin);
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(3, front_chunk.repeat_count);
  }
  // split chunks
  {
    auto& chunk1 =
        chunk_list.Register(SSRingIndex{0}, SSRingIndex{30}, Now(), begin);
    TEST_ASSERT_EQUAL(1, chunk1.repeat_count);
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(3, front_chunk.repeat_count);
    auto& chunk2 =
        chunk_list.Register(SSRingIndex{31}, SSRingIndex{60}, Now(), begin);
    TEST_ASSERT_NOT_EQUAL(&chunk1, &chunk2);
    TEST_ASSERT_EQUAL(1, chunk2.repeat_count);
    chunk2.repeat_count = 2;
  }
}

void test_SendingChunkConfirmPartial() {
  constexpr auto begin = SSRingIndex{0};

  SendingChunkList chunk_list{};
  chunk_list.Register(SSRingIndex{0}, SSRingIndex{1000}, Now(), begin);
  // confirm part
  chunk_list.RemoveUpTo(SSRingIndex{100}, begin);
  {
    TEST_ASSERT_FALSE(chunk_list.empty());
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(101,
                      static_cast<SSRingIndex::type>(front_chunk.begin_offset));
  }
  // confirm all
  chunk_list.RemoveUpTo(SSRingIndex{1000}, SSRingIndex{101});
  {
    TEST_ASSERT_TRUE(chunk_list.empty());
  }
}

}  // namespace ae::test_sending_chunk_list

int test_sending_chunk_list() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkList);
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkListRepeatCount);
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkConfirmPartial);
  return UNITY_END();
}
