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

#include "aether/type_traits.h"

namespace ae::test_concat_arrays {
void test_TwoArrays() {
  constexpr auto arr_int_6 =
      ConcatArrays(std::array<int, 2>{1, 2}, std::array<int, 4>{3, 4, 5, 6});
  static_assert(6 == arr_int_6.size());
  static_assert(2 == arr_int_6[1]);
  static_assert(5 == arr_int_6[4]);

  constexpr auto arr_string_3 =
      ConcatArrays(std::array<std::string_view, 2>{"1", "2"},
                   std::array<std::string_view, 1>{"3, 4, 5, 6"});
  static_assert(3 == arr_string_3.size());
  TEST_ASSERT_EQUAL_STRING("1", arr_string_3[0].data());
  TEST_ASSERT_EQUAL_STRING("2", arr_string_3[1].data());
  TEST_ASSERT_EQUAL_STRING("3, 4, 5, 6", arr_string_3[2].data());
}

void test_ManyArrays() {
  constexpr auto arr_int_6 =
      ConcatArrays(std::array<int, 2>{1, 2}, std::array<int, 3>{3, 4, 5},
                   std::array<int, 1>{6});
  static_assert(6 == arr_int_6.size());
  static_assert(2 == arr_int_6[1]);
  static_assert(5 == arr_int_6[4]);
  static_assert(6 == arr_int_6[5]);
}

}  // namespace ae::test_concat_arrays

int test_concat_arrays() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_concat_arrays::test_TwoArrays);
  RUN_TEST(ae::test_concat_arrays::test_ManyArrays);
  return UNITY_END();
}
