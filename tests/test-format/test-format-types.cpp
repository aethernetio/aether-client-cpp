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
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <string_view>

#include "aether/format/format.h"

namespace ae::test_format_types {
void test_FormatNumbers() {
  auto int8 = Format("{}", static_cast<int>(std::int8_t{42}));
  auto int16 = Format("{}", std::int16_t{42});
  auto int32 = Format("{}", std::int32_t{42});
  auto int64 = Format("{}", std::int64_t{42});

  TEST_ASSERT_EQUAL_STRING("42", int8.data());
  TEST_ASSERT_EQUAL_STRING("42", int16.data());
  TEST_ASSERT_EQUAL_STRING("42", int32.data());
  TEST_ASSERT_EQUAL_STRING("42", int64.data());

  auto uint8 = Format("{}", static_cast<int>(std::uint8_t{42}));
  auto uint16 = Format("{}", std::uint16_t{42});
  auto uint32 = Format("{}", std::uint32_t{42});
  auto uint64 = Format("{}", std::uint64_t{42});

  TEST_ASSERT_EQUAL_STRING("42", uint8.data());
  TEST_ASSERT_EQUAL_STRING("42", uint16.data());
  TEST_ASSERT_EQUAL_STRING("42", uint32.data());
  TEST_ASSERT_EQUAL_STRING("42", uint64.data());
}

void test_FormatFloats() {
  auto float_42 = Format("{}", 4.2F);
  auto float_354 = Format("{}", 35.4F);
  auto double_42 = Format("{}", 4.2);
  auto double_354 = Format("{}", 35.4);

  TEST_ASSERT_EQUAL_STRING("4.2", float_42.data());
  TEST_ASSERT_EQUAL_STRING("35.4", float_354.data());
  TEST_ASSERT_EQUAL_STRING("4.2", double_42.data());
  TEST_ASSERT_EQUAL_STRING("35.4", double_354.data());
}

enum class Int8EnumType : std::int8_t {
  kOne = 1,
  kFortyTwo = 42,
};

enum class Int16EnumType : std::int16_t {
  kOne = 1,
  kFortyTwoAndALittle = 4200,
};

void test_FormatEnums() {
  auto one_8 = Format("{}", Int8EnumType::kOne);
  auto forty_two_8 = Format("{}", Int8EnumType::kFortyTwo);

  TEST_ASSERT_EQUAL_STRING("1", one_8.data());
  TEST_ASSERT_EQUAL_STRING("42", forty_two_8.data());

  auto one_16 = Format("{}", Int16EnumType::kOne);
  auto forty_two_16 = Format("{}", Int16EnumType::kFortyTwoAndALittle);

  TEST_ASSERT_EQUAL_STRING("1", one_16.data());
  TEST_ASSERT_EQUAL_STRING("4200", forty_two_16.data());
}

void test_FormatStrings() {
  auto hello = Format("{}", std::string_view{"hello"});
  auto world = Format("{}", std::string{"world"});

  TEST_ASSERT_EQUAL_STRING("hello", hello.data());
  TEST_ASSERT_EQUAL_STRING("world", world.data());
}

void test_Containers() {
  std::vector<std::int8_t> vec_data = {0x7f, 0x01, 0x42};
  auto vec_data_str = Format("{}", vec_data);
  TEST_ASSERT_EQUAL_STRING("7f0142", vec_data_str.data());

  std::vector<std::string_view> vec_messages = {"hello", "beautiful", "world"};
  auto vec_messages_str = Format("[{}]", vec_messages);
  TEST_ASSERT_EQUAL_STRING("[hello, beautiful, world]",
                           vec_messages_str.data());

  std::list<std::uint8_t> list_data = {0xff, 0x01, 0x42};
  auto list_data_str = Format("{}", list_data);
  TEST_ASSERT_EQUAL_STRING("ff0142", list_data_str.data());

  std::list<Int8EnumType> list_enum = {
      Int8EnumType::kOne, Int8EnumType::kFortyTwo, Int8EnumType::kOne};
  auto list_enum_str = Format("[{}]", list_enum);
  TEST_ASSERT_EQUAL_STRING("[1, 42, 1]", list_enum_str.data());

  auto arr_data = std::array{0xff, 0x01, 0x42};
  auto arr_data_str = Format("{}", arr_data);
  TEST_ASSERT_EQUAL_STRING("255, 1, 66", arr_data_str.data());

  auto arr_floats = std::array{25.5F, 4.2F, 35.4F};
  auto arr_floats_str = Format("{}", arr_floats);
  TEST_ASSERT_EQUAL_STRING("25.5, 4.2, 35.4", arr_floats_str.data());
}
}  // namespace ae::test_format_types

int test_format_types() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_format_types::test_FormatNumbers);
  RUN_TEST(ae::test_format_types::test_FormatFloats);
  RUN_TEST(ae::test_format_types::test_FormatEnums);
  RUN_TEST(ae::test_format_types::test_FormatStrings);
  RUN_TEST(ae::test_format_types::test_Containers);
  return UNITY_END();
}
