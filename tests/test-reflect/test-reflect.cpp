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

#include <string>

#include "aether/type_traits.h"
#include "aether/reflect/reflect.h"

namespace ae::test_reflect {
struct Foo {
  int int_val;
  float float_val;
  std::string hello;

  AE_CLASS_REFLECT(int_val, float_val, hello);
};

struct Empty {
  AE_CLASS_REFLECT();
};

struct NonReflectable {
  int a;
};

struct TwoLevelReflectable {
  int int_val;
  Foo foo;

  AE_CLASS_REFLECT(int_val, foo);
};

void test_IsReflectable() {
  constexpr bool must_be_true = reflect::IsReflectable<Foo>::value;
  constexpr bool must_be_false = reflect::IsReflectable<NonReflectable>::value;
  TEST_ASSERT(must_be_true);
  TEST_ASSERT(!must_be_false);
}

void test_EmptyReflect() {
  auto empty = Empty{};
  auto empty_reflect = reflect::Reflection{empty};
  TEST_ASSERT_EQUAL(0, empty_reflect.size());
  bool applied = false;
  empty_reflect.Apply([&](auto const&... fields) {
    TEST_ASSERT_EQUAL(0, sizeof...(fields));
    applied = true;
  });
  TEST_ASSERT(applied);
}

void test_FooReflection() {
  Foo foo{1, 12.42f, "Hello"};
  auto foo_reflect = reflect::Reflection{foo};
  TEST_ASSERT_EQUAL(3, foo_reflect.size());
  TEST_ASSERT_EQUAL(1, foo_reflect.get<0>());
  TEST_ASSERT_EQUAL_FLOAT(12.42F, foo_reflect.get<1>());
  TEST_ASSERT_EQUAL_STRING("Hello", foo_reflect.get<2>().c_str());
}

void test_FooApply() {
  Foo foo{1, 12.42f, "Hello"};
  auto foo_reflect = reflect::Reflection{foo};
  bool applied = false;
  foo_reflect.Apply([&](auto const&... fields) {
    TEST_ASSERT_EQUAL(1, ArgAt<0>(fields...));
    TEST_ASSERT_EQUAL_FLOAT(12.42f, ArgAt<1>(fields...));
    TEST_ASSERT_EQUAL_STRING("Hello", ArgAt<2>(fields...).c_str());
    applied = true;
  });
  TEST_ASSERT(applied);
}

void test_TwoLevelReflectable() {
  TwoLevelReflectable tlr{12, {42, 65.13F, "World"}};
  auto tlr_reflect = reflect::Reflection{tlr};

  TEST_ASSERT_EQUAL(2, tlr_reflect.size());
  TEST_ASSERT_EQUAL(12, tlr_reflect.get<0>());

  tlr_reflect.Apply([](auto const&... fields) {
    TEST_ASSERT_EQUAL(12, ArgAt<0>(fields...));
    auto foo_refl = reflect::Reflection(ArgAt<1>(fields...));
    foo_refl.Apply([](auto const&... foo_fields) {
      TEST_ASSERT_EQUAL(42, ArgAt<0>(foo_fields...));
      TEST_ASSERT_EQUAL_FLOAT(65.13F, ArgAt<1>(foo_fields...));
      TEST_ASSERT_EQUAL_STRING("World", ArgAt<2>(foo_fields...).c_str());
    });
  });
}
}  // namespace ae::test_reflect

int test_reflect() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_reflect::test_IsReflectable);
  RUN_TEST(ae::test_reflect::test_EmptyReflect);
  RUN_TEST(ae::test_reflect::test_FooReflection);
  RUN_TEST(ae::test_reflect::test_FooApply);
  RUN_TEST(ae::test_reflect::test_TwoLevelReflectable);
  return UNITY_END();
}
