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

#include "aether/safe_stream/details/sending_chunk_list.h"

namespace ae::test_sending_chunk_list {

static constexpr std::size_t kCapacity = 1024;
using IndexType = RingIndex<kCapacity>;
using IndexRangeType = RingIndexRange<IndexType>;
using ChunkList = SendingChunkList<IndexType>;

void test_SendingChunkList() {
  constexpr auto begin = IndexType{0};

  ChunkList chunk_list{begin};

  // add three chunks
  chunk_list.Register(IndexRangeType{IndexType{0}, IndexType{5}}, Now());
  chunk_list.Register(IndexRangeType{IndexType{6}, IndexType{10}}, Now());
  chunk_list.Register(IndexRangeType{IndexType{11}, IndexType{20}}, Now());
  TEST_ASSERT_FALSE(chunk_list.empty());

  auto& ch1 = chunk_list.front();
  TEST_ASSERT_EQUAL(0, ch1.range.left);

  // repeat add first chunk
  chunk_list.Register(IndexRangeType{IndexType{0}, IndexType{5}}, Now());

  // now in front must be the second chunk
  auto& ch2 = chunk_list.front();
  TEST_ASSERT_EQUAL(6, ch2.range.left);

  // add second and third chunks
  chunk_list.Register(IndexRangeType{IndexType{6}, IndexType{10}}, Now());
  chunk_list.Register(IndexRangeType{IndexType{11}, IndexType{20}}, Now());

  // now in front must be the first chunk again
  auto& ch3 = chunk_list.front();
  TEST_ASSERT_EQUAL(0, ch3.range.left);
}

void test_SendingChunkListSelect() {
  constexpr auto begin = IndexType{0};

  ChunkList chunk_list{begin};

  auto* s0 = chunk_list.Select(IndexRangeType{IndexType{6}, IndexType{10}});
  TEST_ASSERT_NULL(s0);

  // add three chunks
  chunk_list.Register(IndexRangeType{IndexType{0}, IndexType{5}}, Now());
  chunk_list.Register(IndexRangeType{IndexType{6}, IndexType{10}}, Now());
  chunk_list.Register(IndexRangeType{IndexType{11}, IndexType{20}}, Now());

  auto* s1 = chunk_list.Select(IndexRangeType{IndexType{6}, IndexType{10}});
  TEST_ASSERT_NOT_NULL(s1);
  TEST_ASSERT_EQUAL(6, s1->range.left);

  auto* s2 = chunk_list.Select(IndexRangeType{IndexType{21}, IndexType{25}});
  TEST_ASSERT_NULL(s2);

  chunk_list.RemoveUpTo(IndexType{21});
  auto* s3 = chunk_list.Select(IndexRangeType{IndexType{6}, IndexType{10}});
  TEST_ASSERT_NULL(s3);
}

void test_SendingChunkListRemoving() {
  constexpr auto begin = IndexType{0};

  ChunkList chunk_list{begin};

  chunk_list.Register(IndexRangeType{IndexType{0}, IndexType{10}}, Now());
  TEST_ASSERT_FALSE(chunk_list.empty());

  // remove it
  chunk_list.RemoveUpTo(IndexType{10});
  TEST_ASSERT_TRUE(chunk_list.empty());

  // add far away chunk
  chunk_list.Register(IndexRangeType{IndexType{100}, IndexType{200}}, Now());
  auto& ch2 = chunk_list.front();
  TEST_ASSERT_EQUAL(100, ch2.range.left);

  // try to remove
  chunk_list.RemoveUpTo(IndexType{10});
  TEST_ASSERT_FALSE(chunk_list.empty());
  {
    auto& front_chunk = chunk_list.front();
    TEST_ASSERT_EQUAL(100, front_chunk.range.left);
  }

  // confirm all
  chunk_list.RemoveUpTo(IndexType{1000});
  TEST_ASSERT_TRUE(chunk_list.empty());
}

}  // namespace ae::test_sending_chunk_list

int test_sending_chunk_list() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkList);
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkListSelect);
  RUN_TEST(ae::test_sending_chunk_list::test_SendingChunkListRemoving);
  return UNITY_END();
}
