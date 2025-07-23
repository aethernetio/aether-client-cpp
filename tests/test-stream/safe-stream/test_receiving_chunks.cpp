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

#include "aether/stream_api/safe_stream/receiving_chunk_list.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_receiving_chunks {
static constexpr std::string_view test_data =
    "The Taste That Goes EXTREME! Radical Refreshment For Maximum Coolness";

void test_AddChunks() {
  auto chunk_list = ReceiveChunkList{};
  auto res = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kAdded, res);
  TEST_ASSERT_EQUAL(1, chunk_list.size());

  // add the same chunk
  res = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), 1});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kAdded, res);
  // must not add the new chunk
  TEST_ASSERT_EQUAL(1, chunk_list.size());

  // add duplicate chunk
  res = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), 1});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kDuplicate, res);
  // must not add the new chunk
  TEST_ASSERT_EQUAL(1, chunk_list.size());

  // add overlapped chunk
  res = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{20}, ToDataBuffer(test_data), {}});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kAdded, res);
  // split the chunk on two
  TEST_ASSERT_EQUAL(2, chunk_list.size());

  // add chunk unorder
  res = chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{test_data.size() * 2}, ToDataBuffer(test_data), {}});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kAdded, res);
  TEST_ASSERT_EQUAL(3, chunk_list.size());

  // return order
  res = chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{test_data.size()}, ToDataBuffer(test_data), {}});
  TEST_ASSERT_EQUAL(ReceiveChunkList::AddResult::kAdded, res);
  // though last chunk is overlapped with 3rd and 4th there still only 3 chunks
  TEST_ASSERT_EQUAL(4, chunk_list.size());
}

void test_ReceiveChunks() {
  auto chunk_list = ReceiveChunkList{};

  auto chunk0 = chunk_list.ReceiveChunk(SSRingIndex{0});
  TEST_ASSERT_FALSE(chunk0.has_value());

  // add 3 chunks one after another
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}});
  chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{static_cast<SSRingIndex::type>(test_data.size())},
      ToDataBuffer(test_data),
      {}});
  chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 2)},
      ToDataBuffer(test_data),
      {}});

  auto chunk1 = chunk_list.ReceiveChunk(SSRingIndex{0});
  TEST_ASSERT_TRUE(chunk1.has_value());
  TEST_ASSERT_EQUAL(test_data.size() * 3, chunk1->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data(),
                               test_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(),
                               chunk1->data.data() + test_data.size(),
                               test_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(),
                               chunk1->data.data() + test_data.size() * 2,
                               test_data.size());

  chunk_list.Acknowledge(SSRingIndex{0}, chunk1->offset_range().right);

  // no more chunks to receive
  auto chunk2 = chunk_list.ReceiveChunk(SSRingIndex{0});
  TEST_ASSERT_FALSE(chunk2.has_value());

  // add 2 chunks with skip
  chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 3)},
      ToDataBuffer(test_data),
      {}});
  chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 5)},
      ToDataBuffer(test_data),
      {}});

  auto start_offset =
      SSRingIndex{static_cast<SSRingIndex::type>(chunk1->data.size())};
  // only the first chunk was popped
  auto chunk3 = chunk_list.ReceiveChunk(start_offset);
  TEST_ASSERT_TRUE(chunk3.has_value());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk3->data.data(),
                               test_data.size());

  chunk_list.Acknowledge(SSRingIndex{0}, chunk3->offset_range().right);

  auto chunk4 = chunk_list.ReceiveChunk(start_offset);
  TEST_ASSERT_FALSE(chunk4.has_value());
}

void test_ReceiveChunksInOrder() {
  auto chunk_list = ReceiveChunkList{};

  // second
  chunk_list.AddChunk(ReceivingChunk{
      SSRingIndex{static_cast<SSRingIndex::type>(test_data.size())},
      ToDataBuffer(test_data),
      {}});
  // first
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}});

  auto chunk1 = chunk_list.ReceiveChunk(SSRingIndex{0});
  TEST_ASSERT_TRUE(chunk1.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(chunk1->offset));
  TEST_ASSERT_EQUAL(test_data.size() * 2, chunk1->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data(),
                               test_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(),
                               chunk1->data.data() + test_data.size(),
                               test_data.size());
}

void test_ReceiveChunksOverlap() {
  auto chunk_list = ReceiveChunkList{};

  // first
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}});
  // second
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{20}, ToDataBuffer(test_data), {}});

  auto chunk1 = chunk_list.ReceiveChunk(SSRingIndex{0});
  TEST_ASSERT_TRUE(chunk1.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(chunk1->offset));
  TEST_ASSERT_EQUAL(test_data.size() + 20, chunk1->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data(), 20);
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data() + 20,
                               test_data.size());

  chunk_list.Acknowledge(SSRingIndex{0}, chunk1->offset_range().right);

  // full overlap
  auto confirmed = SSRingIndex{chunk1->offset_range().right + 1};
  // add 20 bytes
  chunk_list.AddChunk(ReceivingChunk{
      confirmed,
      ToDataBuffer(std::begin(test_data), std::begin(test_data) + 20),
      {}});

  // overlap from the beginning
  chunk_list.AddChunk(ReceivingChunk{confirmed, ToDataBuffer(test_data), {}});

  auto chunk2 = chunk_list.ReceiveChunk(confirmed);
  TEST_ASSERT_TRUE(chunk2.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(confirmed),
                    static_cast<SSRingIndex::type>(chunk2->offset));
  TEST_ASSERT_EQUAL(test_data.size(), chunk2->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk2->data.data(),
                               test_data.size());

  chunk_list.Acknowledge(SSRingIndex{0}, chunk2->offset_range().right);

  // overlap with received
  confirmed = chunk2->offset_range().right + 1;
  // add data with overlap 20 bytes to confirmed offset
  chunk_list.AddChunk(
      ReceivingChunk{confirmed - 20, ToDataBuffer(test_data), {}});
  chunk_list.AddChunk(
      ReceivingChunk{confirmed + 49, ToDataBuffer(test_data), {}});
  auto chunk3 = chunk_list.ReceiveChunk(confirmed);
  TEST_ASSERT_TRUE(chunk3.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(confirmed),
                    static_cast<SSRingIndex::type>(chunk3->offset));
  TEST_ASSERT_EQUAL(test_data.size() - 20 + test_data.size(),
                    chunk3->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data() + 20, chunk3->data.data(),
                               test_data.size() - 20);
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk3->data.data() + 49,
                               test_data.size());

  chunk_list.Acknowledge(SSRingIndex{0}, chunk3->offset_range().right);

  // overlap with confirmed
  confirmed = chunk3->offset_range().right + 1;
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}});
  auto chunk4 = chunk_list.ReceiveChunk(confirmed);
  TEST_ASSERT_FALSE(chunk4.has_value());

  // receive with gap and overoverlap with next chunk
  // first 20 bytes with 20 offset
  chunk_list.AddChunk(ReceivingChunk{
      confirmed + 20,
      ToDataBuffer(std::begin(test_data), std::begin(test_data) + 20),
      {}});
  // full overlap
  chunk_list.AddChunk(ReceivingChunk{confirmed, ToDataBuffer(test_data), {}});

  auto chunk5 = chunk_list.ReceiveChunk(confirmed);
  TEST_ASSERT_TRUE(chunk5.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(confirmed),
                    static_cast<SSRingIndex::type>(chunk5->offset));
  TEST_ASSERT_EQUAL(test_data.size(), chunk5->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk5->data.data(),
                               test_data.size());
}

void test_FindMissedChunks() {
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};

  auto missed0 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_TRUE(missed0.empty());

  chunk_list.AddChunk(ReceivingChunk{begin, ToDataBuffer(test_data), {}});
  chunk_list.AddChunk(
      ReceivingChunk{begin + test_data.size(), ToDataBuffer(test_data), {}});

  auto missed1 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_TRUE(missed1.empty());

  // add chunks with skip
  auto new_begin = begin + test_data.size() * 2;
  chunk_list.AddChunk(ReceivingChunk{
      new_begin + test_data.size(), ToDataBuffer(test_data), {}});

  auto missed2 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_FALSE(missed2.empty());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(new_begin),
                    static_cast<SSRingIndex::type>(missed2[0].expected_offset));

  // check missed chunks from arbitrary offset
  auto missed3 =
      chunk_list.FindMissedChunks(SSRingIndex{begin + test_data.size()});
  TEST_ASSERT_FALSE(missed3.empty());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(new_begin),
                    static_cast<SSRingIndex::type>(missed3[0].expected_offset));
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
