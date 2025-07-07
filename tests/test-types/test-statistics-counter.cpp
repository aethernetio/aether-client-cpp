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

#include "aether/types/statistic_counter.h"

namespace ae::test_statistics_counter {
void test_EmptyCounter() {
  // Create counter with capacity 5
  StatisticsCounter<int, 5> counter;

  // Test size and emptiness
  TEST_ASSERT_EQUAL(0, counter.size());
  TEST_ASSERT_TRUE(counter.empty());
}

void test_SingleValue() {
  StatisticsCounter<int, 5> counter;
  counter.Add(42);

  TEST_ASSERT_EQUAL(1, counter.size());
  TEST_ASSERT_FALSE(counter.empty());
  TEST_ASSERT_EQUAL(42, counter.min());
  TEST_ASSERT_EQUAL(42, counter.max());
  TEST_ASSERT_EQUAL(42, counter.percentile<0>());
  TEST_ASSERT_EQUAL(42, counter.percentile<50>());
  TEST_ASSERT_EQUAL(42, counter.percentile<100>());
}

void test_MultipleValues() {
  StatisticsCounter<int, 5> counter;
  counter.Add(10);
  counter.Add(20);
  counter.Add(30);

  TEST_ASSERT_EQUAL(3, counter.size());
  TEST_ASSERT_FALSE(counter.empty());
  TEST_ASSERT_EQUAL(10, counter.min());
  TEST_ASSERT_EQUAL(30, counter.max());
  TEST_ASSERT_EQUAL(10, counter.percentile<0>());
  TEST_ASSERT_EQUAL(20, counter.percentile<50>());  // Should be median
  TEST_ASSERT_EQUAL(30, counter.percentile<100>());
}

void test_CircularBehavior() {
  StatisticsCounter<int, 3> counter;
  counter.Add(10);
  counter.Add(20);
  counter.Add(30);
  counter.Add(40);  // Should overwrite first value

  TEST_ASSERT_EQUAL(3, counter.size());
  TEST_ASSERT_EQUAL(20, counter.min());
  TEST_ASSERT_EQUAL(40, counter.max());
  TEST_ASSERT_EQUAL(20, counter.percentile<0>());
  TEST_ASSERT_EQUAL(30, counter.percentile<50>());
  TEST_ASSERT_EQUAL(40, counter.percentile<100>());
}

void test_Percentiles() {
  StatisticsCounter<int, 100> counter;

  for (auto i = 1; i <= 100; ++i) {
    counter.Add(i);
  }

  TEST_ASSERT_EQUAL(100, counter.size());
  TEST_ASSERT_EQUAL(1, counter.min());
  TEST_ASSERT_EQUAL(100, counter.max());
  TEST_ASSERT_EQUAL(1, counter.percentile<0>());
  TEST_ASSERT_EQUAL(21, counter.percentile<21>());
  TEST_ASSERT_EQUAL(50, counter.percentile<50>());
  TEST_ASSERT_EQUAL(90, counter.percentile<90>());
  TEST_ASSERT_EQUAL(99, counter.percentile<99>());
  TEST_ASSERT_EQUAL(100, counter.percentile<100>());
}

}  // namespace ae::test_statistics_counter

int test_statistics_counter() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_statistics_counter::test_EmptyCounter);
  RUN_TEST(ae::test_statistics_counter::test_SingleValue);
  RUN_TEST(ae::test_statistics_counter::test_MultipleValues);
  RUN_TEST(ae::test_statistics_counter::test_CircularBehavior);
  RUN_TEST(ae::test_statistics_counter::test_Percentiles);
  return UNITY_END();
}
