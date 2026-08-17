/*
 * Copyright 2026 Aethernet Inc.
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

#include <cstdint>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/types/variant_type.h"

namespace ae::test_variant_type {
using TestVariant =
    VariantType<std::uint8_t, VPair<3, int>, VPair<254, std::uint32_t>,
                VPair<7, std::uint16_t>>;

void test_VariantTypeLoadsLegacyPackets() {
  auto small_packet = std::vector<std::uint8_t>{3, 42, 0, 0, 0};
  auto small_value = TestVariant{};
  auto small_archive =
      seri::BinaryArchive{seri::BinaryVectorBuffer<>{small_packet}};
  TEST_ASSERT(small_archive.Load(small_value));
  TEST_ASSERT_EQUAL_UINT8(3, small_value.Index());
  TEST_ASSERT_EQUAL_INT(42, small_value.Get<int>());

  // Legacy packets use explicit tags, including the high uint8_t boundary.
  auto large_packet = std::vector<std::uint8_t>{254, 0x78, 0x56, 0x34, 0x12};
  auto large_value = TestVariant{};
  auto large_archive =
      seri::BinaryArchive{seri::BinaryVectorBuffer<>{large_packet}};
  TEST_ASSERT(large_archive.Load(large_value));
  TEST_ASSERT_EQUAL_UINT8(254, large_value.Index());
  TEST_ASSERT_EQUAL_UINT32(0x12345678, large_value.Get<std::uint32_t>());
}

void test_VariantTypeSavesLegacyPackets() {
  auto buffer = std::vector<std::uint8_t>{};
  auto value = TestVariant{std::uint16_t{0x1234}};
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{buffer}};

  TEST_ASSERT(archive.Save(value));

  auto const expected = std::vector<std::uint8_t>{7, 0x34, 0x12};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), buffer.data(),
                                expected.size());
}

void test_VariantTypeUnknownTagFails() {
  std::vector<std::uint8_t> buffer{255};
  auto value = TestVariant{std::uint32_t{7}};
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{buffer}};

  TEST_ASSERT(!archive.Load(value));
  TEST_ASSERT_EQUAL_UINT8(254, value.Index());
  TEST_ASSERT_EQUAL_UINT32(7, value.Get<std::uint32_t>());
}

}  // namespace ae::test_variant_type

int test_variant_type() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_variant_type::test_VariantTypeLoadsLegacyPackets);
  RUN_TEST(ae::test_variant_type::test_VariantTypeSavesLegacyPackets);
  RUN_TEST(ae::test_variant_type::test_VariantTypeUnknownTagFails);
  return UNITY_END();
}
