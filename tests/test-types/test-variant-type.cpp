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
#include <limits>
#include <variant>
#include <vector>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"

namespace ae::test_variant_type {
static_assert(std::numeric_limits<std::uint8_t>::max() == 255);
static_assert(std::numeric_limits<std::uint8_t>::max() - 1 == 254);

// Do not instantiate a 255-alternative std::variant here: on Windows x86 MSVC
// it can explode object sections in this test TU. mstream.h already enforces
// the 255-alternative limit with a static_assert.

void test_VariantRoundTripExactType() {
  std::vector<std::uint8_t> buffer;
  std::variant<int, std::uint32_t> value{std::uint32_t{42}};

  {
    auto buffer_writer = VectorWriter<>{buffer};
    auto stream = omstream{buffer_writer};

    stream << value;
  }

  TEST_ASSERT_EQUAL_UINT8(1, buffer[0]);

  std::variant<int, std::uint32_t> loaded{};
  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> loaded;
    TEST_ASSERT(data_was_read(stream));
  }

  TEST_ASSERT_EQUAL(1, loaded.index());
  TEST_ASSERT(std::holds_alternative<std::uint32_t>(loaded));
  TEST_ASSERT_EQUAL_UINT32(42, std::get<std::uint32_t>(loaded));
}

void test_VariantValuelessSentinel() {
  std::vector<std::uint8_t> buffer{std::numeric_limits<std::uint8_t>::max()};
  std::variant<int, std::uint32_t> value{std::uint32_t{7}};

  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> value;
    TEST_ASSERT(data_was_read(stream));
  }

  TEST_ASSERT_EQUAL(1, value.index());
  TEST_ASSERT(std::holds_alternative<std::uint32_t>(value));
  TEST_ASSERT_EQUAL_UINT32(7, std::get<std::uint32_t>(value));
}

void test_VariantUnsupportedBoundaryTag254Fails() {
  std::vector<std::uint8_t> buffer{
      static_cast<std::uint8_t>(std::numeric_limits<std::uint8_t>::max() - 1)};
  std::variant<int, std::uint32_t> value{std::uint32_t{7}};

  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> value;
    TEST_ASSERT(!data_was_read(stream));
  }

  TEST_ASSERT_EQUAL(1, value.index());
  TEST_ASSERT(std::holds_alternative<std::uint32_t>(value));
  TEST_ASSERT_EQUAL_UINT32(7, std::get<std::uint32_t>(value));
}

void test_VariantBoundaryTag254AndSentinel255() {
  std::variant<int, std::uint32_t, std::uint16_t> value{
      std::in_place_index<2>, std::uint16_t{9}};

  std::vector<std::uint8_t> buffer;
  {
    auto buffer_writer = VectorWriter<>{buffer};
    auto stream = omstream{buffer_writer};

    stream << value;
  }

  TEST_ASSERT_EQUAL_UINT8(2, buffer[0]);

  std::variant<int, std::uint32_t, std::uint16_t> loaded{};
  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> loaded;
    TEST_ASSERT(data_was_read(stream));
  }

  TEST_ASSERT_EQUAL(2, loaded.index());
  TEST_ASSERT(std::holds_alternative<std::uint16_t>(loaded));
  TEST_ASSERT_EQUAL_UINT16(9, std::get<std::uint16_t>(loaded));

  std::vector<std::uint8_t> sentinel_buffer{std::numeric_limits<std::uint8_t>::max()};
  auto sentinel = std::variant<int, std::uint32_t>{std::in_place_index<0>, 3};
  {
    auto buffer_reader = VectorReader<>{sentinel_buffer};
    auto stream = imstream{buffer_reader};

    stream >> sentinel;
    TEST_ASSERT(data_was_read(stream));
  }

  TEST_ASSERT_EQUAL(0, sentinel.index());
  TEST_ASSERT(std::holds_alternative<int>(sentinel));
  TEST_ASSERT_EQUAL_INT(3, std::get<int>(sentinel));
}

}  // namespace ae::test_variant_type

int test_variant_type() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_variant_type::test_VariantRoundTripExactType);
  RUN_TEST(ae::test_variant_type::test_VariantValuelessSentinel);
  RUN_TEST(ae::test_variant_type::test_VariantUnsupportedBoundaryTag254Fails);
  RUN_TEST(ae::test_variant_type::test_VariantBoundaryTag254AndSentinel255);
  return UNITY_END();
}
