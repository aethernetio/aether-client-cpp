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
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};
  auto added_1 = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}}, begin,
      begin);
  TEST_ASSERT_TRUE(added_1.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(added_1->offset));

  // add the same chunk
  auto added_2 = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), 1}, begin, begin);
  TEST_ASSERT_TRUE(added_2.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(added_2->offset));
  // must be the old repeat count
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(added_2->repeat_count));

  // add overlapped chunk
  auto added_3 = chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{20}, ToDataBuffer(test_data), {}}, begin,
      begin);
  TEST_ASSERT_TRUE(added_3.has_value());
  TEST_ASSERT_EQUAL(20, static_cast<SSRingIndex::type>(added_3->offset));

  // add chunk unorder
  auto added_4 = chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{test_data.size() * 2}, ToDataBuffer(test_data), {}},
      begin, begin);
  TEST_ASSERT_TRUE(added_4.has_value());
  TEST_ASSERT_EQUAL(test_data.size() * 2,
                    static_cast<SSRingIndex::type>(added_4->offset));

  // return order
  auto added_5 = chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{test_data.size()}, ToDataBuffer(test_data), {}},
      begin, begin);
  TEST_ASSERT_TRUE(added_5.has_value());
  TEST_ASSERT_EQUAL(test_data.size(),
                    static_cast<SSRingIndex::type>(added_5->offset));
}

void test_PopChunks() {
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};

  auto chunk0 = chunk_list.PopChunks(begin);
  TEST_ASSERT_FALSE(chunk0.has_value());

  // add 3 chunks one after another
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}}, begin,
      begin);
  chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{static_cast<SSRingIndex::type>(test_data.size())},
          ToDataBuffer(test_data),
          {}},
      begin, begin);
  chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 2)},
          ToDataBuffer(test_data),
          {}},
      begin, begin);

  auto chunk1 = chunk_list.PopChunks(begin);
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

  // no more chunks
  auto chunk2 = chunk_list.PopChunks(begin);
  TEST_ASSERT_FALSE(chunk2.has_value());

  // add 2 chunks with skip
  chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 3)},
          ToDataBuffer(test_data),
          {}},
      begin, begin);
  chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{static_cast<SSRingIndex::type>(test_data.size() * 5)},
          ToDataBuffer(test_data),
          {}},
      begin, begin);

  // only the first chunk was popped
  auto chunk3 = chunk_list.PopChunks(
      SSRingIndex{static_cast<SSRingIndex::type>(chunk1->data.size())});
  TEST_ASSERT_TRUE(chunk3.has_value());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk3->data.data(),
                               test_data.size());

  auto chunk4 = chunk_list.PopChunks(begin);
  TEST_ASSERT_FALSE(chunk4.has_value());
}

void test_PopChunksInOrder() {
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};

  // second
  chunk_list.AddChunk(
      ReceivingChunk{
          SSRingIndex{static_cast<SSRingIndex::type>(test_data.size())},
          ToDataBuffer(test_data),
          {}},
      begin, begin);
  // first
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}}, begin,
      begin);

  auto chunk1 = chunk_list.PopChunks(begin);
  TEST_ASSERT_TRUE(chunk1.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(chunk1->offset));
  TEST_ASSERT_EQUAL(test_data.size() * 2, chunk1->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data(),
                               test_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(),
                               chunk1->data.data() + test_data.size(),
                               test_data.size());
}

void test_PopChunksOverlap() {
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};

  // first
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}}, begin,
      begin);
  // second
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{20}, ToDataBuffer(test_data), {}}, begin,
      begin);

  auto chunk1 = chunk_list.PopChunks(begin);
  TEST_ASSERT_TRUE(chunk1.has_value());
  TEST_ASSERT_EQUAL(0, static_cast<SSRingIndex::type>(chunk1->offset));
  TEST_ASSERT_EQUAL(test_data.size() + 20, chunk1->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk1->data.data(),
                               test_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data() + test_data.size() - 20,
                               chunk1->data.data() + test_data.size(), 20);

  // full overlap
  auto confirmed =
      SSRingIndex{static_cast<SSRingIndex::type>(chunk1->data.size())};
  // add 20 bytes
  chunk_list.AddChunk(ReceivingChunk{confirmed,
                                     ToDataBuffer(std::begin(test_data),
                                                  std::begin(test_data) + 20),
                                     {}},
                      confirmed, begin);

  // overlap from the beginning
  chunk_list.AddChunk(ReceivingChunk{confirmed, ToDataBuffer(test_data), {}},
                      confirmed, begin);
  auto chunk2 = chunk_list.PopChunks(confirmed);
  TEST_ASSERT_TRUE(chunk2.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(confirmed),
                    static_cast<SSRingIndex::type>(chunk2->offset));
  TEST_ASSERT_EQUAL(test_data.size(), chunk2->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk2->data.data(),
                               test_data.size());

  // overlap with received
  confirmed =
      chunk2->offset + static_cast<SSRingIndex::type>(chunk2->data.size());
  chunk_list.AddChunk(ReceivingChunk{confirmed, ToDataBuffer(test_data), {}},
                      confirmed, begin);
  // same data but with 20 bytes offset and 20 byte length
  chunk_list.AddChunk(ReceivingChunk{confirmed + 20,
                                     ToDataBuffer(std::begin(test_data),
                                                  std::begin(test_data) + 20),
                                     {}},
                      confirmed, begin);
  auto chunk3 = chunk_list.PopChunks(confirmed);
  TEST_ASSERT_TRUE(chunk3.has_value());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(confirmed),
                    static_cast<SSRingIndex::type>(chunk3->offset));
  TEST_ASSERT_EQUAL(test_data.size(), chunk3->data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_data.data(), chunk3->data.data(),
                               test_data.size());
  // overlap with confirmed
  confirmed =
      chunk3->offset + static_cast<SSRingIndex::type>(chunk3->data.size());
  chunk_list.AddChunk(
      ReceivingChunk{SSRingIndex{0}, ToDataBuffer(test_data), {}}, confirmed,
      begin);
  auto chunk4 = chunk_list.PopChunks(confirmed);
  TEST_ASSERT_FALSE(chunk4.has_value());
}

void test_FindMissedChunks() {
  constexpr auto begin = SSRingIndex{0};
  auto chunk_list = ReceiveChunkList{};

  auto missed0 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_TRUE(missed0.empty());

  chunk_list.AddChunk(ReceivingChunk{begin, ToDataBuffer(test_data), {}}, begin,
                      begin);
  chunk_list.AddChunk(
      ReceivingChunk{begin + test_data.size(), ToDataBuffer(test_data), {}},
      begin, begin);

  auto missed1 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_TRUE(missed1.empty());

  // add chunks with skip
  auto new_begin = begin + test_data.size() * 2;
  chunk_list.AddChunk(
      ReceivingChunk{new_begin + test_data.size(), ToDataBuffer(test_data), {}},
      begin, begin);

  auto missed2 = chunk_list.FindMissedChunks(begin);
  TEST_ASSERT_FALSE(missed2.empty());
  TEST_ASSERT_EQUAL(static_cast<SSRingIndex::type>(new_begin),
                    static_cast<SSRingIndex::type>(missed2[0].expected_offset));
}
}  // namespace ae::test_receiving_chunks

int test_receiving_chunks() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_receiving_chunks::test_AddChunks);
  RUN_TEST(ae::test_receiving_chunks::test_PopChunks);
  RUN_TEST(ae::test_receiving_chunks::test_PopChunksInOrder);
  RUN_TEST(ae::test_receiving_chunks::test_PopChunksOverlap);
  RUN_TEST(ae::test_receiving_chunks::test_FindMissedChunks);
  return UNITY_END();
}
