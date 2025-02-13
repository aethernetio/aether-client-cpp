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

#include <list>
#include <chrono>

#include "aether/common.h"
#include "aether/format/format.h"

namespace ae::test_format_time {
void test_FormatTimePoint() {
  auto tp = TimePoint{std::chrono::milliseconds{9913675}};
  auto tp_str = Format("=:-{:%H:%M:%S}", tp);
  TEST_ASSERT_EQUAL_STRING("=:-02:45:13.675000", tp_str.data());

  auto tp2 = TimePoint{std::chrono::milliseconds{9913675}};
  auto tp2_str = Format("[{:%H:%M:%S}]", tp2);
  TEST_ASSERT_EQUAL_STRING("[02:45:13.675000]", tp2_str.data());
}

void test_FormatListOfTimePoint() {
  auto tp_list = std::list{TimePoint{std::chrono::milliseconds{13675}},
                           TimePoint{std::chrono::milliseconds{14000}},
                           TimePoint{std::chrono::milliseconds{15888}}};
  auto tp_list_str = Format("{:o=%S}", tp_list);
  TEST_ASSERT_EQUAL_STRING("o=13.675000, o=14.000000, o=15.888000",
                           tp_list_str.data());
}
}  // namespace ae::test_format_time

int test_format_time() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_format_time::test_FormatTimePoint);
  RUN_TEST(ae::test_format_time::test_FormatListOfTimePoint);
  return UNITY_END();
}
