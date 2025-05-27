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

#include "aether/types/span.h"

namespace ae::test_span {
void test_CreateSpan() {
  constexpr auto string_span = Span{"Hello!"};
  static_assert(7 == string_span.size(), "Size should be same and constexpr");
  TEST_ASSERT_EQUAL_STRING_LEN("Hello!", string_span.data(),
                               string_span.size());

  static constexpr auto arr = std::array{12, 42, 54};
  constexpr auto arr_span = Span{arr};
  static_assert(arr.size() == arr_span.size(),
                "Size should be same and constexpr");
  TEST_ASSERT_EQUAL_INT_ARRAY(arr.data(), arr_span.data(), arr.size());

  // constexpr auto temp_arr_span = Span{{1, 2, 3}}; <- unable to create from
  // temporary object

  std::size_t i = 0;
  for (auto v : arr_span) {
    TEST_ASSERT_EQUAL(arr[i++], v);
  }
}
};  // namespace ae::test_span

int test_span() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_span::test_CreateSpan);
  return UNITY_END();
}
