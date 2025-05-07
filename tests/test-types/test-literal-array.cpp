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

#include "unity.h"

#include "aether/literal_array.h"

#include "aether/crc.h"

namespace ae::test_literal_array {
#define GET_CRC(token) crc32::checksum_from_literal(#token);

void test_LiteralArray() {
  constexpr auto crc1 = crc32::checksum_from_literal("asd");
  constexpr auto crc2 = GET_CRC(asd);
  static_assert(crc1 == crc2, "as");

  // test size
  {
    constexpr auto arr1 = MakeLiteralArray("aafc");
    static_assert(arr1.value_.size() == 2);
    constexpr auto arr2 = MakeLiteralArray("aaf");
    static_assert(arr2.value_.size() == 2);
    constexpr auto arr3 = MakeLiteralArray("aafc1");
    static_assert(arr3.value_.size() == 3);
  }
  // test values
  {
    constexpr auto arr1 = MakeLiteralArray("aafc");
    static_assert(0xAA == arr1.value_[0]);
    static_assert(0xFC == arr1.value_[1]);
    TEST_ASSERT_EQUAL(0xAA, arr1.value_[0]);
    TEST_ASSERT_EQUAL(0xFC, arr1.value_[1]);

    constexpr auto arr2 = MakeLiteralArray("aaf");
    TEST_ASSERT_EQUAL(0xAA, arr2.value_[0]);
    TEST_ASSERT_EQUAL(0x0F, arr2.value_[1]);

    constexpr auto arr3 = MakeLiteralArray("AAFC1");
    TEST_ASSERT_EQUAL(0xAA, arr3.value_[0]);
    TEST_ASSERT_EQUAL(0xFC, arr3.value_[1]);
    TEST_ASSERT_EQUAL(0x01, arr3.value_[2]);
  }
  // convert ot array
  {
    constexpr auto arr1 = static_cast<
        std::array<std::uint8_t, 32>>(MakeLiteralArray(
        "EDA3B86DCFC1CBCCFE2F4408B69C3C622EAC4F868D7CA4D9C0C576A1A6C87B44"));

    std::uint8_t expected[] = {0xED, 0xA3, 0xB8, 0x6D, 0xCF, 0xC1, 0xCB, 0xCC,
                               0xFE, 0x2F, 0x44, 0x08, 0xB6, 0x9C, 0x3C, 0x62,
                               0x2E, 0xAC, 0x4F, 0x86, 0x8D, 0x7C, 0xA4, 0xD9,
                               0xC0, 0xC5, 0x76, 0xA1, 0xA6, 0xC8, 0x7B, 0x44};

    static_assert(0xED == arr1[0]);
    static_assert(0xA3 == arr1[1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, arr1.data(), arr1.size());

    auto lit_array_2 = MakeLiteralArray(
        "346B06C1D264D9ED9F48021F43CA582A66A2560B693F99E01381E1DC9BE825D1EDA3B8"
        "6DCFC1CBCCFE2F4408B69C3C622EAC4F868D7CA4D9C0C576A1A6C87B44");
    auto arr2 = static_cast<std::array<std::uint8_t, 64>>(lit_array_2);

    std::uint8_t expected_2[] = {
        0x34, 0x6B, 0x06, 0xC1, 0xD2, 0x64, 0xD9, 0xED, 0x9F, 0x48, 0x02,
        0x1F, 0x43, 0xCA, 0x58, 0x2A, 0x66, 0xA2, 0x56, 0x0B, 0x69, 0x3F,
        0x99, 0xE0, 0x13, 0x81, 0xE1, 0xDC, 0x9B, 0xE8, 0x25, 0xD1, 0xED,
        0xA3, 0xB8, 0x6D, 0xCF, 0xC1, 0xCB, 0xCC, 0xFE, 0x2F, 0x44, 0x08,
        0xB6, 0x9C, 0x3C, 0x62, 0x2E, 0xAC, 0x4F, 0x86, 0x8D, 0x7C, 0xA4,
        0xD9, 0xC0, 0xC5, 0x76, 0xA1, 0xA6, 0xC8, 0x7B, 0x44};

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_2, arr2.data(), arr2.size());
  }
}
}  // namespace ae::test_literal_array

int test_literal_array() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_literal_array::test_LiteralArray);
  return UNITY_END();
}
