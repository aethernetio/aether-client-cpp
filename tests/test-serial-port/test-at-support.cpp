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

#include "aether/serial_ports/at_support/at_support.h"

namespace ae::test_at_support {
void test_ParseAtResponse() {
  std::string_view str = "#XRECV: 104";
  DataBuffer buffer{
      reinterpret_cast<std::uint8_t const*>(str.data()),
      reinterpret_cast<std::uint8_t const*>(str.data() + str.size())};
  std::size_t size;
  auto parse_end = AtSupport::ParseResponse(buffer, "#XRECV", size);
  TEST_ASSERT_TRUE(parse_end.has_value());
  TEST_ASSERT_EQUAL(str.size(), *parse_end);
  TEST_ASSERT_EQUAL(104, size);
}

void test_ParseAtResponseTwoValues() {
  std::string_view str = "#XRECV: 104,105";
  DataBuffer buffer{
      reinterpret_cast<std::uint8_t const*>(str.data()),
      reinterpret_cast<std::uint8_t const*>(str.data() + str.size())};
  std::size_t size;
  std::size_t count;
  auto parse_end = AtSupport::ParseResponse(buffer, "#XRECV", size, count);
  TEST_ASSERT_TRUE(parse_end.has_value());
  TEST_ASSERT_EQUAL(str.size(), *parse_end);
  TEST_ASSERT_EQUAL(104, size);
  TEST_ASSERT_EQUAL(105, count);
}

void test_ParseAtNoValue() {
  std::string_view str = "#XRECV:";
  DataBuffer buffer{
      reinterpret_cast<std::uint8_t const*>(str.data()),
      reinterpret_cast<std::uint8_t const*>(str.data() + str.size())};
  std::size_t size{22};
  auto parse_end = AtSupport::ParseResponse(buffer, "#XRECV", size);
  TEST_ASSERT_FALSE(parse_end.has_value());
  TEST_ASSERT_EQUAL(22, size);
}

void test_ParseAtResponseTwoNoValue() {
  std::string_view str = "#XRECV: 12";
  DataBuffer buffer{
      reinterpret_cast<std::uint8_t const*>(str.data()),
      reinterpret_cast<std::uint8_t const*>(str.data() + str.size())};
  std::size_t size{22};
  std::size_t count{10};
  auto parse_end = AtSupport::ParseResponse(buffer, "#XRECV", size, count);
  TEST_ASSERT_FALSE(parse_end.has_value());
  TEST_ASSERT_EQUAL(12, size);
  TEST_ASSERT_EQUAL(10, count);
}

}  // namespace ae::test_at_support

int test_at_support() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_at_support::test_ParseAtResponse);
  RUN_TEST(ae::test_at_support::test_ParseAtResponseTwoValues);
  RUN_TEST(ae::test_at_support::test_ParseAtNoValue);
  RUN_TEST(ae::test_at_support::test_ParseAtResponseTwoNoValue);
  return UNITY_END();
}
