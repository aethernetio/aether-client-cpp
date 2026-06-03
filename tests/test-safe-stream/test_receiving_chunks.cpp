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

#include "aether/safe_stream/details/receiving_chunk_list.h"

namespace ae::test_receiving_chunks {
static constexpr std::size_t kCapacity = 1024;
using IndexType = RingIndex<kCapacity>;
using IndexRangeType = RingIndexRange<IndexType>;
using ChunkList = ReceiveChunkList<IndexType>;

void test_AddChunks() {
  static constexpr IndexType buffer_begin = IndexType{0};
  auto chunk_list = ChunkList{buffer_begin};

  auto res =
      chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{69}}, 0);
  TEST_ASSERT_EQUAL(ChunkAddResult::kAdded, res);

  // add the same chunk
  res = chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{69}}, 1);
  TEST_ASSERT_EQUAL(ChunkAddResult::kAddRepeated, res);

  // add duplicate chunk
  res = chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{69}}, 1);
  TEST_ASSERT_EQUAL(ChunkAddResult::kDuplicate, res);

  // add overlapped chunk
  res = chunk_list.AddChunk(IndexRangeType{IndexType{20}, IndexType{89}}, 0);
  TEST_ASSERT_EQUAL(ChunkAddResult::kAdded, res);

  // add chunk unorder
  res = chunk_list.AddChunk(IndexRangeType{IndexType{140}, IndexType{209}}, 0);
  TEST_ASSERT_EQUAL(ChunkAddResult::kAdded, res);

  // return order
  res = chunk_list.AddChunk(IndexRangeType{IndexType{70}, IndexType{139}}, 0);
  TEST_ASSERT_EQUAL(ChunkAddResult::kAdded, res);
}

void test_ReceiveChunks() {
  static constexpr IndexType buffer_begin = IndexType{0};
  auto chunk_list = ChunkList{buffer_begin};

  auto chunk0 = chunk_list.ReceiveChunk();
  TEST_ASSERT_TRUE(chunk0.IsEmpty());

  // add 3 chunks one after another
  chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{49}}, 0);
  chunk_list.AddChunk(IndexRangeType{IndexType{50}, IndexType{99}}, 0);
  chunk_list.AddChunk(IndexRangeType{IndexType{100}, IndexType{149}}, 0);

  auto chunk1 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk1.IsEmpty());
  TEST_ASSERT_EQUAL(0, chunk1.left);
  TEST_ASSERT_EQUAL(149, chunk1.right);

  chunk_list.Acknowledge(chunk1.right);
  chunk_list.set_buffer_begin(chunk1.right + 1);

  // no more chunks to receive
  auto chunk2 = chunk_list.ReceiveChunk();
  TEST_ASSERT_TRUE(chunk2.IsEmpty());

  // add 2 chunks with skip
  chunk_list.AddChunk(IndexRangeType{IndexType{150}, IndexType{199}}, 0);
  chunk_list.AddChunk(IndexRangeType{IndexType{250}, IndexType{299}}, 0);

  // only the first chunk was popped
  auto chunk3 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk3.IsEmpty());
  TEST_ASSERT_EQUAL(150, chunk3.left);
  TEST_ASSERT_EQUAL(199, chunk3.right);

  chunk_list.Acknowledge(chunk3.right);
  chunk_list.set_buffer_begin(chunk1.right + 1);

  auto chunk4 = chunk_list.ReceiveChunk();
  TEST_ASSERT_TRUE(chunk4.IsEmpty());
}

void test_ReceiveChunksInOrder() {
  static constexpr IndexType buffer_begin = IndexType{0};
  auto chunk_list = ChunkList{buffer_begin};

  // second
  chunk_list.AddChunk(IndexRangeType{IndexType{100}, IndexType{149}}, 0);
  // first
  chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{99}}, 0);

  auto chunk1 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk1.IsEmpty());
  TEST_ASSERT_EQUAL(0, chunk1.left);
  TEST_ASSERT_EQUAL(149, chunk1.right);
}

void test_ReceiveChunksOverlap() {
  static constexpr IndexType buffer_begin = IndexType{0};
  auto chunk_list = ChunkList{buffer_begin};

  // first
  chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{99}}, 0);
  // second
  chunk_list.AddChunk(IndexRangeType{IndexType{20}, IndexType{119}}, 0);

  auto chunk1 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk1.IsEmpty());
  TEST_ASSERT_EQUAL(0, chunk1.left);
  TEST_ASSERT_EQUAL(119, chunk1.right);

  chunk_list.Acknowledge(chunk1.right);
  chunk_list.set_buffer_begin(chunk1.right + 1);

  // full overlap
  auto acknowledged = IndexType{chunk1.right + 1};
  // add 20 bytes
  chunk_list.AddChunk(
      IndexRangeType{acknowledged, IndexType{acknowledged + 20}}, 0);

  // overlap from the beginning
  chunk_list.AddChunk(
      IndexRangeType{acknowledged, IndexType{acknowledged + 50}}, 0);

  auto chunk2 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk2.IsEmpty());

  TEST_ASSERT_EQUAL(acknowledged, chunk2.left);
  TEST_ASSERT_EQUAL(acknowledged + 50, chunk2.right);

  chunk_list.Acknowledge(chunk2.right);
  chunk_list.set_buffer_begin(chunk2.right + 1);
  acknowledged = chunk2.right + 1;

  // receive with gap and overoverlap with next chunk
  // first 20 bytes with 20 offset
  chunk_list.AddChunk(IndexRangeType{IndexType{acknowledged + 20},
                                     IndexType{acknowledged + 49}},
                      0);
  // full overlap
  chunk_list.AddChunk(
      IndexRangeType{IndexType{acknowledged}, IndexType{acknowledged + 49}}, 0);

  auto chunk5 = chunk_list.ReceiveChunk();
  TEST_ASSERT_FALSE(chunk5.IsEmpty());
  TEST_ASSERT_EQUAL(acknowledged, chunk5.left);
  TEST_ASSERT_EQUAL(acknowledged + 49, chunk5.right);
}

void test_FindMissedChunks() {
  static constexpr IndexType buffer_begin = IndexType{0};
  auto chunk_list = ChunkList{buffer_begin};

  auto missed0 = chunk_list.FindMissedChunk();
  TEST_ASSERT_FALSE(missed0.has_value());

  chunk_list.AddChunk(IndexRangeType{IndexType{0}, IndexType{10}}, 0);
  chunk_list.AddChunk(IndexRangeType{IndexType{11}, IndexType{30}}, 0);

  auto missed1 = chunk_list.FindMissedChunk();
  TEST_ASSERT_FALSE(missed1.has_value());

  // add chunks with skip
  chunk_list.AddChunk(IndexRangeType{IndexType{50}, IndexType{79}}, 0);

  auto missed2 = chunk_list.FindMissedChunk();
  TEST_ASSERT_TRUE(missed2.has_value());
  TEST_ASSERT_EQUAL(31, missed2->left);
  TEST_ASSERT_EQUAL(49, missed2->right);
}
}  // namespace ae::test_receiving_chunks

int test_receiving_chunks() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_receiving_chunks::test_AddChunks);
  RUN_TEST(ae::test_receiving_chunks::test_ReceiveChunks);
  RUN_TEST(ae::test_receiving_chunks::test_ReceiveChunksInOrder);
  RUN_TEST(ae::test_receiving_chunks::test_ReceiveChunksOverlap);
  RUN_TEST(ae::test_receiving_chunks::test_FindMissedChunks);
  return UNITY_END();
}
