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

#include <cstdint>

#include "aether/ring_buffer.h"

namespace ae::test_ring_buffer {

void test_RingBufferShifting() {
  using U8RI = RingIndex<std::uint8_t, 10>;

  auto b1 = U8RI{0};

  auto b2 = b1 + 1;
  TEST_ASSERT_EQUAL(1, static_cast<std::uint8_t>(b2));

  auto b3 = b1 - 1;
  TEST_ASSERT_EQUAL(10, static_cast<std::uint8_t>(b3));

  auto b4 = b1 + 10;
  TEST_ASSERT_EQUAL(10, static_cast<std::uint8_t>(b4));

  auto b5 = b1 + 9;
  TEST_ASSERT_EQUAL(9, static_cast<std::uint8_t>(b5));

  auto b6 = b1 + 11;
  TEST_ASSERT_EQUAL(0, static_cast<std::uint8_t>(b6));

  auto b7 = b1 - 10;
  TEST_ASSERT_EQUAL(1, static_cast<std::uint8_t>(b7));

  auto b8 = b1 - 8;
  TEST_ASSERT_EQUAL(3, static_cast<std::uint8_t>(b8));

  using U8RI_MAX = RingIndex<std::uint8_t>;
  auto a1 = U8RI_MAX{};
  auto a2 = a1 + 1;
  TEST_ASSERT_EQUAL(1, static_cast<std::uint8_t>(a2));
  auto a3 = a1 + 255;
  TEST_ASSERT_EQUAL(255, static_cast<std::uint8_t>(a3));
  auto a3_1 = a3 + 1;
  TEST_ASSERT_EQUAL(0, static_cast<std::uint8_t>(a3_1));
  auto a4 = a1 - 200;
  TEST_ASSERT_EQUAL(56, static_cast<std::uint8_t>(a4));
}

void test_RingBufferDistance() {
  using U8RI = RingIndex<std::uint8_t, 10>;
  auto b1 = U8RI{0};

  auto d1 = b1.Distance(b1 + 1);
  TEST_ASSERT_EQUAL(1, d1);

  auto d2 = b1.Distance(b1 + 9);
  TEST_ASSERT_EQUAL(9, d2);

  auto d3 = b1.Distance(b1 + 10);
  TEST_ASSERT_EQUAL(10, d3);

  auto b2 = U8RI{9};
  auto d4 = b2.Distance(b2 + 5);
  TEST_ASSERT_EQUAL(5, d4);
}

void test_RingBufferCompare() {
  using U8RI = RingIndex<std::uint8_t, 50>;
  constexpr auto begin = U8RI{0};
  auto a = U8RI{10};
  auto b = U8RI{15};

  TEST_ASSERT_TRUE(a(begin) < b);
  TEST_ASSERT_TRUE(b(begin) > a);
  TEST_ASSERT_FALSE(a(begin) > b);
  TEST_ASSERT_FALSE(b(begin) < a);
  TEST_ASSERT_TRUE(a(begin) == a);
  TEST_ASSERT_TRUE(a(begin) != b);
  TEST_ASSERT_FALSE(a(begin) == b);
  TEST_ASSERT_FALSE(a(begin) != a);
  TEST_ASSERT_TRUE(a(begin) >= a);
  TEST_ASSERT_TRUE(b(begin) >= a);
  TEST_ASSERT_FALSE(a(begin) >= b);
  TEST_ASSERT_TRUE(a(begin) <= b);
  TEST_ASSERT_TRUE(a(begin) <= a);
  TEST_ASSERT_FALSE(b(begin) <= a);
}

}  // namespace ae::test_ring_buffer

int test_ring_buffer() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ring_buffer::test_RingBufferShifting);
  RUN_TEST(ae::test_ring_buffer::test_RingBufferDistance);
  RUN_TEST(ae::test_ring_buffer::test_RingBufferCompare);
  return UNITY_END();
}
