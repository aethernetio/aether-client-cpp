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

#include "aether/types/ring_index.h"

namespace ae::test_ring_index {
void test_RingIndexShifting() {
  using Index = RingIndex<10>;

  auto b1 = Index{0};

  auto b2 = b1 + 1;
  TEST_ASSERT_EQUAL(1, static_cast<std::size_t>(b2));

  auto b3 = b1 - 1;
  TEST_ASSERT_EQUAL(9, static_cast<std::size_t>(b3));

  auto b4 = b1 + 10;
  TEST_ASSERT_EQUAL(0, static_cast<std::size_t>(b4));

  auto b5 = b1 + 9;
  TEST_ASSERT_EQUAL(9, static_cast<std::size_t>(b5));

  auto b6 = b1 + 11;
  TEST_ASSERT_EQUAL(1, static_cast<std::size_t>(b6));

  auto b7 = b1 - 10;
  TEST_ASSERT_EQUAL(0, static_cast<std::size_t>(b7));

  auto b8 = b1 - 8;
  TEST_ASSERT_EQUAL(2, static_cast<std::size_t>(b8));

  auto b9 = Index{6} + 5;
  TEST_ASSERT_EQUAL(1, static_cast<std::size_t>(b9));

  using Index_Max = RingIndex<std::numeric_limits<std::uint8_t>::max()>;
  auto a1 = Index_Max{0};
  auto a2 = a1 + 1;
  TEST_ASSERT_EQUAL(1, static_cast<std::uint8_t>(a2));
  auto a3 = a1 + 255;
  TEST_ASSERT_EQUAL(0, static_cast<std::uint8_t>(a3));
  auto a3_1 = a3 + 1;
  TEST_ASSERT_EQUAL(1, static_cast<std::uint8_t>(a3_1));
  auto a4 = a1 - 200;
  TEST_ASSERT_EQUAL(55, static_cast<std::uint8_t>(a4));
}

void test_RingIndexDistance() {
  using Index = RingIndex<10>;
  auto b1 = Index{0};

  auto d1 = b1.Distance(b1 + 1);
  TEST_ASSERT_EQUAL(1, d1);

  auto d2 = b1.Distance(b1 + 9);
  TEST_ASSERT_EQUAL(9, d2);

  auto d3 = b1.Distance(b1 + 10);
  TEST_ASSERT_EQUAL(0, d3);

  auto b2 = Index{9};
  auto d4 = b2.Distance(b2 + 5);
  TEST_ASSERT_EQUAL(5, d4);
}

void test_RingIndexCompare() {
  using Index = RingIndex<50>;
  constexpr auto begin = Index{0};
  auto a = Index{10};
  auto b = Index{15};

  TEST_ASSERT_TRUE((IndexComparable{a, begin} < b));
  TEST_ASSERT_TRUE((IndexComparable{b, begin} > a));
  TEST_ASSERT_FALSE((IndexComparable{a, begin} > b));
  TEST_ASSERT_FALSE((IndexComparable{b, begin} < a));
  TEST_ASSERT_TRUE(a == a);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_FALSE(a == b);
  TEST_ASSERT_FALSE(a != a);
}

void test_RangeIndex() {
  using Index = RingIndex<50>;
  using IndexRange = RingIndexRange<Index>;

  constexpr auto begin = Index{0};

  auto a = IndexRange{Index{0}, Index{10}};
  TEST_ASSERT_TRUE(a.InRange(Index{0}, begin));
  TEST_ASSERT_TRUE(a.InRange(Index{10}, begin));
  TEST_ASSERT_TRUE(a.InRange(Index{5}, begin));
  TEST_ASSERT_TRUE(a.InRange(Index{4}, begin));
  TEST_ASSERT_FALSE(a.InRange(Index{11}, begin));
  TEST_ASSERT_TRUE(a.IsBefore(Index{11}, begin));
  TEST_ASSERT_FALSE(a.IsAfter(Index{11}, begin));
  TEST_ASSERT_TRUE(a.IsAfter(Index{56}, Index{55}));
  TEST_ASSERT_FALSE(a.IsBefore(Index{56}, Index{55}));
}

}  // namespace ae::test_ring_index

int test_ring_index() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ring_index::test_RingIndexShifting);
  RUN_TEST(ae::test_ring_index::test_RingIndexDistance);
  RUN_TEST(ae::test_ring_index::test_RingIndexCompare);
  RUN_TEST(ae::test_ring_index::test_RangeIndex);
  return UNITY_END();
}
