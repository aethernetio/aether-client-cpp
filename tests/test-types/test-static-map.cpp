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

#include "aether/types/static_map.h"

namespace ae::test_static_map {
void test_CreateStaticMap() {
  constexpr auto map_i_i = StaticMap{std::array{
      std::pair{1, 12},
      std::pair{2, 42},
      std::pair{3, 56},
  }};

  static_assert(3 == map_i_i.size(), "Size should be constexpr");

  auto const* f_1 = map_i_i.find(1);
  TEST_ASSERT_EQUAL(1, f_1->first);
  TEST_ASSERT_EQUAL(12, f_1->second);
  TEST_ASSERT_NOT_EQUAL(std::end(map_i_i), f_1);

  auto const* f_3 = map_i_i.find(3);
  TEST_ASSERT_EQUAL(3, f_3->first);
  TEST_ASSERT_EQUAL(56, f_3->second);
  TEST_ASSERT_NOT_EQUAL(std::end(map_i_i), f_1);

  auto const* f_4 = map_i_i.find(4);
  TEST_ASSERT_EQUAL(std::end(map_i_i), f_4);
}
}  // namespace ae::test_static_map

int test_static_map() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_static_map::test_CreateStaticMap);
  return UNITY_END();
}
