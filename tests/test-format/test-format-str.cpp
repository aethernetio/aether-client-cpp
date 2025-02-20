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

#include "aether/format/format.h"

namespace ae::test_format_str {
void test_BracketInTheBegin() {
  auto str1 = Format("{ kek");
  TEST_ASSERT_EQUAL_STRING("{ kek", str1.c_str());
  auto str2 = Format("{} kek");
  TEST_ASSERT_EQUAL_STRING(" kek", str2.c_str());

  auto str3 = Format("} kek {");
  TEST_ASSERT_EQUAL_STRING("} kek {", str3.c_str());
}

void test_BracketInTheEnd() {
  auto str1 = Format("kek {");
  TEST_ASSERT_EQUAL_STRING("kek {", str1.c_str());
  auto str2 = Format("kek {}");
  TEST_ASSERT_EQUAL_STRING("kek ", str2.c_str());
}

void test_BracketEscape() {
  auto str1 = Format("kek {{}}");
  TEST_ASSERT_EQUAL_STRING("kek {}", str1.c_str());

  auto str2 = Format("{} kek {{}}");
  TEST_ASSERT_EQUAL_STRING(" kek {}", str2.c_str());

  auto str3 = Format("{} kek {{}}", 12, 42);
  TEST_ASSERT_EQUAL_STRING("12 kek {42}", str3.c_str());
}
}  // namespace ae::test_format_str

int test_format_str() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_format_str::test_BracketInTheBegin);
  RUN_TEST(ae::test_format_str::test_BracketInTheEnd);
  RUN_TEST(ae::test_format_str::test_BracketEscape);
  return UNITY_END();
}
