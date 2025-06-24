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

#include "aether/types/uid.h"

namespace ae::test_uid {
static constexpr std::uint8_t expected_value[] = {
    0xf8, 0x1d, 0x4f, 0xae, 0x7d, 0xec, 0x11, 0xd0,
    0xa7, 0x65, 0x00, 0xa0, 0xc9, 0x1e, 0x6b, 0xf6};

static constexpr char kValidUid[] = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";
static constexpr char kValidUidUpper[] = "F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6";
static constexpr char kInvalidUidShort[] = "f81d4fae-7dec-11d0-a765-00a0c91e6b";
static constexpr char kInvalidUidWrongFormat[] =
    "f81d4fae7dec-11d0-a76500a0c91e6bf612";

void test_UidFromStrLiteral() {
  constexpr auto uid_valid = UidString{kValidUid};
  TEST_ASSERT(uid_valid.valid);

  constexpr auto uid_valid_upper = UidString{kValidUidUpper};
  TEST_ASSERT(uid_valid_upper.valid);

  constexpr auto uid_invalid_short = UidString{kInvalidUidShort};
  TEST_ASSERT_FALSE(uid_invalid_short.valid);

  constexpr auto uid_invalid_wrong_format = UidString{kInvalidUidWrongFormat};
  TEST_ASSERT_FALSE(uid_invalid_wrong_format.valid);

  constexpr auto uid = Uid::FromString(kValidUid);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_value, uid.value.data(),
                                sizeof(expected_value));
  TEST_ASSERT_FALSE(uid.empty());

  constexpr auto uid_upper = Uid::FromString(kValidUidUpper);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_value, uid_upper.value.data(),
                                sizeof(expected_value));
  TEST_ASSERT_FALSE(uid_upper.empty());

  constexpr auto uid_short = Uid::FromString(kInvalidUidShort);
  TEST_ASSERT(uid_short.empty());

  constexpr auto uid_wrong_format = Uid::FromString(kInvalidUidWrongFormat);
  TEST_ASSERT(uid_wrong_format.empty());
}

void test_UidFromStrView() {
  auto uid_valid = UidString{std::string_view{kValidUid}};
  TEST_ASSERT(uid_valid.valid);

  auto uid_valid_upper = UidString{std::string_view{kValidUidUpper}};
  TEST_ASSERT(uid_valid_upper.valid);

  auto uid_invalid_short = UidString{std::string_view{kInvalidUidShort}};
  TEST_ASSERT_FALSE(uid_invalid_short.valid);

  auto uid_invalid_wrong_format =
      UidString{std::string_view{kInvalidUidWrongFormat}};
  TEST_ASSERT_FALSE(uid_invalid_wrong_format.valid);

  auto uid = Uid::FromString(std::string_view{kValidUid});
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_value, uid.value.data(),
                                sizeof(expected_value));
  TEST_ASSERT_FALSE(uid.empty());

  auto uid_upper = Uid::FromString(std::string_view{kValidUidUpper});
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_value, uid_upper.value.data(),
                                sizeof(expected_value));
  TEST_ASSERT_FALSE(uid_upper.empty());

  auto uid_short = Uid::FromString(std::string_view{kInvalidUidShort});
  TEST_ASSERT(uid_short.empty());

  auto uid_wrong_format =
      Uid::FromString(std::string_view{kInvalidUidWrongFormat});
  TEST_ASSERT(uid_wrong_format.empty());
}

}  // namespace ae::test_uid

int test_uid() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_uid::test_UidFromStrLiteral);
  RUN_TEST(ae::test_uid::test_UidFromStrView);
  return UNITY_END();
}
