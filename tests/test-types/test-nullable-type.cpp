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

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"

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

struct Bar {
  AE_REFLECT_MEMBERS(value, enabled);
  std::optional<int> value;
  bool enabled;
};

struct Foo : Bar, NullableType<Foo> {
  AE_REFLECT(AE_BASE(Bar));
};

void test_SaveLoadNullableType() {
  std::vector<std::uint8_t> buffer;
  NoOptionalData data;
  data.a = 42;
  data.c = true;

  // save
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(data.Seri(archive));
  }

  AssertPacket(buffer, std::uint8_t{}, int{42}, bool{true});
  NoOptionalData load_data{};
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(load_data.Deseri(archive));
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
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(data_has_value.Seri(archive));
    TEST_ASSERT(data_no_has_value.Seri(archive));
  }

  AssertPacket(buffer, std::uint8_t{}, int{42}, bool{true}, std::uint8_t{0x1},
               bool{true});

  WithOptionalData load_data_has_value{};
  WithOptionalData load_data_no_has_value{};
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(load_data_has_value.Deseri(archive));
    TEST_ASSERT(load_data_no_has_value.Deseri(archive));
  }
  TEST_ASSERT(load_data_has_value.b.has_value());
  TEST_ASSERT(load_data_has_value.b == data_has_value.b);
  TEST_ASSERT(load_data_has_value.c == data_has_value.c);

  TEST_ASSERT(!load_data_no_has_value.b.has_value());
  TEST_ASSERT(load_data_no_has_value.c == data_no_has_value.c);
}

void test_OptionalDataAbsentClearsPrepopulatedValue() {
  std::vector<std::uint8_t> buffer;
  WithOptionalData data{};
  data.c = true;
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(data.Seri(archive));
  }

  WithOptionalData load_data{};
  load_data.b = 42;
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(load_data.Deseri(archive));
  }
  TEST_ASSERT(!load_data.b.has_value());
  TEST_ASSERT(load_data.c == data.c);
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
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(data.Seri(archive));
    TEST_ASSERT(data_opt_value.Seri(archive));
  }

  AssertPacket(buffer, std::uint8_t{0x4}, int{42}, bool{true}, bool{true},
               std::uint8_t{}, int{412}, bool{true}, int{12}, bool{true});
  BasedOnNoOption load_data{};
  BasedOnNoOption load_data_opt_value{};
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(load_data.Deseri(archive));
    TEST_ASSERT(load_data_opt_value.Deseri(archive));
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

void test_InheritedOptionalWithBaseReflection() {
  std::vector<std::uint8_t> buffer;
  Foo without_value{};
  without_value.enabled = true;
  Foo with_value{};
  with_value.value = 42;
  with_value.enabled = true;

  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(without_value.Seri(archive));
    TEST_ASSERT(with_value.Seri(archive));
  }

  AssertPacket(buffer, std::uint8_t{0x1}, bool{true}, std::uint8_t{}, int{42},
               bool{true});

  Foo loaded_without_value{};
  loaded_without_value.value = 7;
  Foo loaded_with_value{};
  {
    auto archive = seri::BinaryArchive{VectorBuffer<PackedSize>{buffer}};
    TEST_ASSERT(loaded_without_value.Deseri(archive));
    TEST_ASSERT(loaded_with_value.Deseri(archive));
  }

  TEST_ASSERT(!loaded_without_value.value.has_value());
  TEST_ASSERT(loaded_without_value.enabled == without_value.enabled);
  TEST_ASSERT(loaded_with_value.value == with_value.value);
  TEST_ASSERT(loaded_with_value.enabled == with_value.enabled);
}

}  // namespace ae::test_nullable_type

int test_nullable_type() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_nullable_type::test_SaveLoadNullableType);
  RUN_TEST(ae::test_nullable_type::test_OptionalData);
  RUN_TEST(
      ae::test_nullable_type::test_OptionalDataAbsentClearsPrepopulatedValue);
  RUN_TEST(ae::test_nullable_type::test_WithDerived);
  RUN_TEST(ae::test_nullable_type::test_InheritedOptionalWithBaseReflection);
  return UNITY_END();
}
