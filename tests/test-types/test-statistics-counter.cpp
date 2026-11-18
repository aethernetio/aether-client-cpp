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

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/types/ping_statistics.h"
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
  TEST_ASSERT_EQUAL(22, counter.percentile<21>());
  TEST_ASSERT_EQUAL(51, counter.percentile<50>());
  TEST_ASSERT_EQUAL(91, counter.percentile<90>());
  TEST_ASSERT_EQUAL(100, counter.percentile<99>());
  TEST_ASSERT_EQUAL(100, counter.percentile<100>());
}

void test_PackedPingPercentilesAndEviction() {
  PingStatistics<100> history;
  history.Add(Duration::max());
  history.Add(Duration{1});
  std::array<Duration, 100> ordered{};
  for (std::uint32_t i = 0; i < ordered.size(); ++i) {
    // Shuffle the insertion order. Include ties and replies several seconds
    // late.
    auto const group = (i * 53U) % 100U / 2U;
    auto const sample = Duration{20'000U + group * 200'000U};
    PingStatistics<1> single;
    single.Add(sample);
    ordered[i] = single.percentile<0>();
    history.Add(sample);
  }
  std::sort(ordered.begin(), ordered.end());

  TEST_ASSERT_EQUAL(100, history.size());
  TEST_ASSERT_EQUAL_UINT32(ordered[0].count(), history.percentile<0>().count());
  TEST_ASSERT_EQUAL_UINT32(ordered[95].count(),
                           history.percentile<95>().count());
  TEST_ASSERT_EQUAL_UINT32(
      ordered[95].count(),
      history.PercentileValue(Percentile::FromPercent(95)).count());
  TEST_ASSERT_EQUAL_UINT32(ordered[99].count(),
                           history.percentile<99>().count());
  TEST_ASSERT_UINT32_WITHIN(200'000, 9'420'000,
                            history.percentile<95>().count());
  TEST_ASSERT_EQUAL_UINT32(ordered[99].count(),
                           history.percentile<100>().count());
}

void test_PackedPingArchiveRoundTrip() {
  PingStatistics<4> history;
  history.Add(Duration{0});
  history.Add(Duration{50'000});
  history.Add(Duration{6'000'000});
  history.Add(Duration::max());

  std::vector<std::uint8_t> bytes;
  auto writer = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT(writer.Save(history));
  auto reader = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  std::size_t packed_size{};
  TEST_ASSERT(reader.buffer().Read(seri::SizeTag{packed_size}));
  TEST_ASSERT_EQUAL(6, packed_size);

  PingStatistics<4> restored;
  auto roundtrip = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT(roundtrip.Load(restored));
  TEST_ASSERT_EQUAL(history.size(), restored.size());
  TEST_ASSERT_EQUAL_UINT32(0, restored.percentile<0>().count());
  TEST_ASSERT_EQUAL_UINT32(history.percentile<50>().count(),
                           restored.percentile<50>().count());
  TEST_ASSERT_UINT32_WITHIN(120'000, 6'000'000,
                            restored.percentile<50>().count());
  TEST_ASSERT_EQUAL_UINT32(Duration::max().count(),
                           restored.percentile<95>().count());

  bytes.pop_back();
  auto truncated = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(truncated.Load(restored));
  TEST_ASSERT_EQUAL(4, restored.size());
}

}  // namespace ae::test_statistics_counter

int test_statistics_counter() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_statistics_counter::test_EmptyCounter);
  RUN_TEST(ae::test_statistics_counter::test_SingleValue);
  RUN_TEST(ae::test_statistics_counter::test_MultipleValues);
  RUN_TEST(ae::test_statistics_counter::test_CircularBehavior);
  RUN_TEST(ae::test_statistics_counter::test_Percentiles);
  RUN_TEST(ae::test_statistics_counter::test_PackedPingPercentilesAndEviction);
  RUN_TEST(ae::test_statistics_counter::test_PackedPingArchiveRoundTrip);
  return UNITY_END();
}
