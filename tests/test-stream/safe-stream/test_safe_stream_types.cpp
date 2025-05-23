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

#include "aether/stream_api/safe_stream/safe_stream_types.h"

namespace ae::test_safe_stream_types {
void test_OffsetRange() {
  constexpr auto half_window_range =
      OffsetRange{SSRingIndex{25}, SSRingIndex{75}, SSRingIndex{0}};

  TEST_ASSERT_TRUE(half_window_range.After(SSRingIndex{24}));
  TEST_ASSERT_FALSE(half_window_range.After(SSRingIndex{25}));
  TEST_ASSERT_FALSE(half_window_range.After(SSRingIndex{26}));

  TEST_ASSERT_TRUE(half_window_range.Before(SSRingIndex{76}));
  TEST_ASSERT_FALSE(half_window_range.Before(SSRingIndex{75}));
  TEST_ASSERT_FALSE(half_window_range.Before(SSRingIndex{74}));

  TEST_ASSERT_TRUE(half_window_range.InRange(SSRingIndex{25}));
  TEST_ASSERT_TRUE(half_window_range.InRange(SSRingIndex{75}));
  TEST_ASSERT_TRUE(half_window_range.InRange(SSRingIndex{50}));
  TEST_ASSERT_FALSE(half_window_range.InRange(SSRingIndex{20}));
  TEST_ASSERT_FALSE(half_window_range.InRange(SSRingIndex{80}));

  constexpr auto window_range =
      OffsetRange{SSRingIndex{0}, SSRingIndex{100}, SSRingIndex{0}};
  TEST_ASSERT_TRUE(window_range.InRange(SSRingIndex{0}));
  TEST_ASSERT_TRUE(window_range.InRange(SSRingIndex{100}));
  TEST_ASSERT_TRUE(window_range.InRange(SSRingIndex{10}));
  TEST_ASSERT_FALSE(window_range.InRange(SSRingIndex{110}));
  TEST_ASSERT_TRUE(window_range.Before(SSRingIndex{110}));
  TEST_ASSERT_FALSE(window_range.After(SSRingIndex{110}));
}

}  // namespace ae::test_safe_stream_types

int test_safe_stream_types() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_safe_stream_types::test_OffsetRange);
  return UNITY_END();
}
