/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/safe_stream/details/circular_buffer.h"

#include "tests/test-stream/to_data_buffer.h"

namespace ae::test_circular_buffer {

static constexpr std::string_view test_str = "Let it circle";

void test_PushData() {
  static constexpr std::size_t kCapacity = 30;
  using Buffer = CircularBuffer<kCapacity>;

  Buffer buffer;

  auto res1 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res1.IsOk());
  TEST_ASSERT_EQUAL(0, res1.value().left);
  TEST_ASSERT_EQUAL(test_str.size() - 1, res1.value().right);

  auto res2 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res2.IsOk());
  TEST_ASSERT_EQUAL(test_str.size(), res2.value().left);
  TEST_ASSERT_EQUAL(2 * test_str.size() - 1, res2.value().right);

  // must be overflow
  auto res3 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_FALSE(res3.IsOk());
  TEST_ASSERT_EQUAL(CircularBufferError::kDataOverflow, res3.error());
}

void test_ReadData() {
  static constexpr std::size_t kCapacity = 30;
  using Buffer = CircularBuffer<kCapacity>;
  using IndexType = Buffer::index_type;
  using IndexRangeType = Buffer::index_range_type;

  Buffer buffer;

  auto read_res1 = buffer.Read(IndexType{0}, 10);
  TEST_ASSERT_FALSE(read_res1.IsOk());
  TEST_ASSERT_EQUAL(CircularBufferError::kEmptyBuffer, read_res1.error());

  auto push_res1 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(push_res1.IsOk());

  auto read_res2 = buffer.Read(IndexType{0}, 10);
  TEST_ASSERT_TRUE(read_res2.IsOk());
  TEST_ASSERT_EQUAL(10, read_res2.value().first.size());
  TEST_ASSERT_TRUE(read_res2.value().second.empty());
  TEST_ASSERT_EQUAL_STRING_LEN(test_str.data(), read_res2.value().first.data(),
                               10);

  auto read_res3 = buffer.Read(IndexType{10}, 3);
  TEST_ASSERT_TRUE(read_res3.IsOk());
  TEST_ASSERT_EQUAL(3, read_res3.value().first.size());
  TEST_ASSERT_TRUE(read_res3.value().second.empty());
  TEST_ASSERT_EQUAL_STRING_LEN(test_str.data() + 10,
                               read_res3.value().first.data(), 3);

  auto read_res4 = buffer.Read(IndexType{0}, 20);
  TEST_ASSERT_TRUE(read_res4.IsOk());
  TEST_ASSERT_EQUAL(13, read_res4.value().first.size());
  TEST_ASSERT_TRUE(read_res4.value().second.empty());
  TEST_ASSERT_EQUAL_STRING_LEN(test_str.data(), read_res4.value().first.data(),
                               13);

  auto read_res5 = buffer.Read(IndexType{20}, 13);
  TEST_ASSERT_FALSE(read_res5.IsOk());
  TEST_ASSERT_EQUAL(CircularBufferError::kIndexOutOfRange, read_res5.error());
}

void test_Erase() {
  static constexpr std::size_t kCapacity = 30;
  using Buffer = CircularBuffer<kCapacity>;
  using IndexType = Buffer::index_type;
  using IndexRangeType = Buffer::index_range_type;
  Buffer buffer;

  auto res1 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res1.IsOk());
  auto read_res1 = buffer.Read(IndexType{0}, 3);
  TEST_ASSERT_TRUE(read_res1.IsOk());

  buffer.Erase(IndexType{5});
  auto read_res2 = buffer.Read(IndexType{0}, 13);
  TEST_ASSERT_FALSE(read_res2.IsOk());
  TEST_ASSERT_EQUAL(CircularBufferError::kIndexOutOfRange, read_res2.error());

  auto read_res3 = buffer.Read(IndexType{5}, 8);
  TEST_ASSERT_TRUE(read_res3.IsOk());
  TEST_ASSERT_EQUAL(8, read_res3.value().first.size());
  TEST_ASSERT_EQUAL_STRING_LEN(test_str.data() + 5,
                               read_res3.value().first.data(), 8);

  buffer.Erase(res1.value().right + 1);
  auto read_res4 = buffer.Read(res1.value().right + 1, 3);
  TEST_ASSERT_FALSE(read_res4.IsOk());
  TEST_ASSERT_EQUAL(CircularBufferError::kEmptyBuffer, read_res4.error());
}

void test_PushOverTheBourder() {
  static constexpr std::size_t kCapacity = 30;
  using Buffer = CircularBuffer<kCapacity>;
  using IndexType = Buffer::index_type;
  using IndexRangeType = Buffer::index_range_type;

  // Push some data near the full of the buffer
  // Erase data to make room for more
  // Push more data to wrap buffer around

  Buffer buffer;
  auto res1 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res1.IsOk());
  auto res2 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res2.IsOk());
  buffer.Erase(res1.value().right);
  auto res3 = buffer.Push(ToSpan(test_str));
  TEST_ASSERT_TRUE(res3.IsOk());
  TEST_ASSERT_EQUAL(26, res3.value().left);
  TEST_ASSERT_EQUAL(8, res3.value().right);
}

}  // namespace ae::test_circular_buffer

int test_circular_buffer() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_circular_buffer::test_PushData);
  RUN_TEST(ae::test_circular_buffer::test_ReadData);
  RUN_TEST(ae::test_circular_buffer::test_Erase);
  RUN_TEST(ae::test_circular_buffer::test_PushOverTheBourder);
  return UNITY_END();
}
