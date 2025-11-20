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

#include <vector>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/types/nullable_type.h"

#include "tests/test-api-protocol/assert_packet.h"

namespace ae::test_nullable_type {
struct NoOptionalData : NullableType<NoOptionalData> {
  AE_REFLECT_MEMBERS(a, c);
  int a;
  bool c;
};

struct WithOptionalData : NullableType<WithOptionalData> {
  AE_REFLECT_MEMBERS(b, c);
  std::optional<int> b;
  bool c;
};

struct Base {
  AE_REFLECT_MEMBERS(a, c)
  int a;
  bool c;
};

struct BasedOnNoOption : Base, NullableType<BasedOnNoOption> {
  AE_REFLECT(AE_REF_BASE(Base), AE_MMBRS(b, d));
  std::optional<int> b;
  bool d;
};

void test_SaveLoadNullableType() {
  std::vector<std::uint8_t> buffer;
  NoOptionalData data;
  data.a = 42;
  data.c = true;

  // save
  {
    auto buffer_writer = VectorWriter<>{buffer};
    auto stream = omstream{buffer_writer};

    stream << data;
  }

  AssertPacket(buffer, std::uint8_t{}, int{42}, bool{true});
  NoOptionalData load_data{};
  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> load_data;
  }
  TEST_ASSERT(load_data.a == data.a);
  TEST_ASSERT(load_data.c == data.c);
}

void test_OptionalData() {
  std::vector<std::uint8_t> buffer;

  WithOptionalData data_has_value{};
  data_has_value.b = 42;
  data_has_value.c = true;
  WithOptionalData data_no_has_value{};
  data_no_has_value.c = true;

  // save
  {
    auto buffer_writer = VectorWriter<>{buffer};
    auto stream = omstream{buffer_writer};

    stream << data_has_value << data_no_has_value;
  }

  AssertPacket(buffer, std::uint8_t{}, int{42}, bool{true}, std::uint8_t{0x1},
               bool{true});

  WithOptionalData load_data_has_value{};
  WithOptionalData load_data_no_has_value{};
  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> load_data_has_value >> load_data_no_has_value;
  }
  TEST_ASSERT(load_data_has_value.b.has_value());
  TEST_ASSERT(load_data_has_value.b == data_has_value.b);
  TEST_ASSERT(load_data_has_value.c == data_has_value.c);

  TEST_ASSERT(!load_data_no_has_value.b.has_value());
  TEST_ASSERT(load_data_no_has_value.c == data_no_has_value.c);
}

void test_WithDerived() {
  std::vector<std::uint8_t> buffer;
  BasedOnNoOption data{};
  data.a = 42;
  data.b = {};
  data.c = true;
  data.d = true;
  BasedOnNoOption data_opt_value{};
  data_opt_value.a = 412;
  data_opt_value.b = 12;
  data_opt_value.c = true;
  data_opt_value.d = true;

  // save
  {
    auto buffer_writer = VectorWriter<>{buffer};
    auto stream = omstream{buffer_writer};

    stream << data << data_opt_value;
  }

  AssertPacket(buffer, std::uint8_t{0x4}, int{42}, bool{true}, bool{true},
               std::uint8_t{}, int{412}, bool{true}, int{12}, bool{true});
  BasedOnNoOption load_data{};
  BasedOnNoOption load_data_opt_value{};
  {
    auto buffer_reader = VectorReader<>{buffer};
    auto stream = imstream{buffer_reader};

    stream >> load_data >> load_data_opt_value;
  }
  TEST_ASSERT(load_data.a == data.a);
  TEST_ASSERT(!load_data.b.has_value());
  TEST_ASSERT(load_data.c == data.c);
  TEST_ASSERT(load_data.d == data.d);
  TEST_ASSERT(load_data_opt_value.a == data_opt_value.a);
  TEST_ASSERT(load_data_opt_value.b.has_value());
  TEST_ASSERT(load_data_opt_value.b == data_opt_value.b);
  TEST_ASSERT(load_data_opt_value.c == data_opt_value.c);
  TEST_ASSERT(load_data_opt_value.d == data_opt_value.d);
}

}  // namespace ae::test_nullable_type

int test_nullable_type() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_nullable_type::test_SaveLoadNullableType);
  RUN_TEST(ae::test_nullable_type::test_OptionalData);
  RUN_TEST(ae::test_nullable_type::test_WithDerived);
  return UNITY_END();
}
