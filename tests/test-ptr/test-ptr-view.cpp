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

#include "aether/ptr/ptr.h"
#include "aether/ptr/ptr_view.h"

namespace ae::test_ptr_view {
struct A {
  virtual ~A() { a_destroy_count++; }
  static inline int a_destroy_count = 0;
};
struct B : public A {
  ~B() override { b_destroy_count++; }
  static inline int b_destroy_count = 0;
};

void test_CreatePtrView() {
  A::a_destroy_count = 0;
  B::b_destroy_count = 0;
  {
    auto a = PtrView<A>{};
    auto b = PtrView<B>{};
    TEST_ASSERT(!a);
    TEST_ASSERT(!b);
  }
  TEST_ASSERT_EQUAL(0, A::a_destroy_count);
  TEST_ASSERT_EQUAL(0, B::b_destroy_count);

  A::a_destroy_count = 0;
  {
    auto a = MakePtr<A>();
    auto a_view = PtrView<A>{a};
    auto a_view_copy = a_view;
    TEST_ASSERT(a_view);
    TEST_ASSERT(a_view_copy);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroy_count);
  A::a_destroy_count = 0;
  {
    auto a = MakePtr<A>();
    auto a_view = PtrView<A>{a};
    auto a_view_move_copy = std::move(a_view);
    TEST_ASSERT(!a_view);
    TEST_ASSERT(a_view_move_copy);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroy_count);
  A::a_destroy_count = 0;
  {
    auto a_view = PtrView<A>{};
    TEST_ASSERT(!a_view);
    {
      auto a = MakePtr<A>();
      a_view = a;
      TEST_ASSERT(a_view);
    }
    TEST_ASSERT(!a_view);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroy_count);
}

void test_LockPtrView() {
  A::a_destroy_count = 0;
  {
    auto a_view = PtrView<A>{};
    TEST_ASSERT(!a_view);
    auto a_lock = a_view.Lock();
    TEST_ASSERT(!a_lock);
  }
  TEST_ASSERT_EQUAL(0, A::a_destroy_count);

  A::a_destroy_count = 0;
  {
    auto a = MakePtr<A>();
    auto a_view = PtrView{a};
    TEST_ASSERT(a_view);
    {
      auto a_lock = a_view.Lock();
      TEST_ASSERT(a_lock);
    }
  }
  TEST_ASSERT_EQUAL(1, A::a_destroy_count);
}

}  // namespace ae::test_ptr_view

int test_ptr_view() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ptr_view::test_CreatePtrView);
  RUN_TEST(ae::test_ptr_view::test_LockPtrView);
  return UNITY_END();
}
