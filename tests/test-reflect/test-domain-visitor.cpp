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

#include <map>
#include <list>
#include <vector>

#include "aether/reflect/override_func.h"
#include "aether/reflect/domain_visitor.h"

namespace ae::reflect::test_domain_visitor {
struct Foo {
  int int_val;
  float float_val;
  char const* hello;

  AE_REFLECT_MEMBERS(int_val, float_val, hello);
};

struct NoReflect {
  int int_val;
};

struct FooContainers {
  std::vector<Foo> vec_foos;
  std::list<Foo> list_foos;
  std::map<int, Foo> map_foos;

  AE_REFLECT_MEMBERS(vec_foos, list_foos, map_foos);
};

struct BCycle;
struct ACycle {
  int int_val;
  BCycle* b_ptr;
  AE_REFLECT_MEMBERS(int_val, b_ptr)
};

struct BCycle {
  float float_val;
  ACycle* a_ptr;
  AE_REFLECT_MEMBERS(float_val, a_ptr)
};

void test_VisitFoo() {
  auto foo = Foo{42, 42.3F, "Hello"};
  bool visited_int = false;
  bool visited_float = false;
  bool visited_str = false;
  DomainVisit(foo, OverrideFunc{
                       [&](int v) {
                         TEST_ASSERT_EQUAL(42, v);
                         visited_int = true;
                       },
                       [&](float v) {
                         TEST_ASSERT_EQUAL_FLOAT(42.3F, v);
                         visited_float = true;
                       },
                       [&](char const* v) {
                         TEST_ASSERT_EQUAL_STRING("Hello", v);
                         visited_str = true;
                       },
                   });
  TEST_ASSERT(visited_int);
  TEST_ASSERT(visited_float);
  TEST_ASSERT(visited_str);
}

void test_NoReflect() {
  auto no_reflect = NoReflect{42};
  bool visited = false;
  DomainVisit(no_reflect, [&](auto v) { visited = true; });
  TEST_ASSERT(!visited);
}

void test_FooContainers() {
  auto foo = Foo{42, 42.12F, "World"};
  auto foo_containers = FooContainers{
      {foo, foo, foo},
      {foo, foo, foo, foo},
      {{1, foo}, {2, foo}, {3, foo}},
  };

  int visited_int = 0;
  int visited_float = 0;
  int visited_str = 0;

  auto visitor = OverrideFunc{[&](int v) {
                                TEST_ASSERT_EQUAL(42, v);
                                ++visited_int;
                              },
                              [&](float v) {
                                TEST_ASSERT_EQUAL_FLOAT(42.12F, v);
                                ++visited_float;
                              },
                              [&](char const* v) {
                                TEST_ASSERT_EQUAL_STRING("World", v);
                                ++visited_str;
                              },
                              [&](auto const& v) {}};

  DomainVisit(
      foo_containers,
      DomainNodeVisitor<decltype(visitor)&, VisitPolicy::kDeep>{visitor});

  constexpr auto b = NodeVisitor<std::vector<int>>::Policy::Match(
      VisitPolicy::kDeep | VisitPolicy::kAny);

  TEST_ASSERT_EQUAL(10, visited_int);
  TEST_ASSERT_EQUAL(10, visited_float);
  TEST_ASSERT_EQUAL(10, visited_str);
}

void test_NotPropagateInMapAndList() {
  auto foo = Foo{42, 42.12F, "World"};
  auto foo_containers = FooContainers{
      {foo, foo, foo},
      {foo, foo, foo, foo},
      {{1, foo}, {2, foo}, {3, foo}},
  };

  int visited_int = 0;
  int visited_float = 0;
  int visited_str = 0;

  auto visitor = OverrideFunc{
      [&](int v) {
        TEST_ASSERT_EQUAL(42, v);
        ++visited_int;
      },
      [&](float v) {
        TEST_ASSERT_EQUAL_FLOAT(42.12F, v);
        ++visited_float;
      },
      [&](char const* v) {
        TEST_ASSERT_EQUAL_STRING("World", v);
        ++visited_str;
      },
      [&](auto const& v) {
        return !std::is_same_v<std::map<int, Foo> const&, decltype(v)> &&
               !std::is_same_v<std::list<Foo> const&, decltype(v)>;
      }};

  DomainVisit(
      foo_containers,
      DomainNodeVisitor<decltype(visitor)&, VisitPolicy::kAny>{visitor});

  TEST_ASSERT_EQUAL(3, visited_int);
  TEST_ASSERT_EQUAL(3, visited_float);
  TEST_ASSERT_EQUAL(3, visited_str);
}

void test_CycleDetection() {
  auto a_val = ACycle{12, {}};
  auto b_val = BCycle{12.42F, {}};
  a_val.b_ptr = &b_val;
  b_val.a_ptr = &a_val;

  int a_visited = 0;
  int int_visited = 0;
  int b_visited = 0;
  int float_visited = 0;

  auto visitor = OverrideFunc{[&](ACycle const* val) { ++a_visited; },
                              [&](BCycle const* val) { ++b_visited; },
                              [&](int) { ++int_visited; },
                              [&](float) { ++float_visited; }};

  DomainVisit(
      a_val, DomainNodeVisitor<decltype(visitor)&, VisitPolicy::kAny>{visitor});

  TEST_ASSERT_EQUAL(1, a_visited);
  TEST_ASSERT_EQUAL(1, b_visited);
  TEST_ASSERT_EQUAL(1, int_visited);
  TEST_ASSERT_EQUAL(1, float_visited);
}
}  // namespace ae::reflect::test_domain_visitor

int test_domain_visitor() {
  UNITY_BEGIN();
  RUN_TEST(ae::reflect::test_domain_visitor::test_VisitFoo);
  RUN_TEST(ae::reflect::test_domain_visitor::test_NoReflect);
  RUN_TEST(ae::reflect::test_domain_visitor::test_FooContainers);
  RUN_TEST(ae::reflect::test_domain_visitor::test_NotPropagateInMapAndList);
  RUN_TEST(ae::reflect::test_domain_visitor::test_CycleDetection);
  return UNITY_END();
}
