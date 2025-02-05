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

#include <vector>

#include "aether/ptr/rc_ptr.h"

#include "tests/test-ptr/pipe_there_they_are_sitting.h"

namespace ae::test_rc_ptr {
using test_ptr::A;
using test_ptr::B;

void test_createPtr() {
  {
    RcPtr<A> a;
    RcPtr<A> a1{nullptr};
  }
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    TEST_ASSERT(a);
  }
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1{a};
    TEST_ASSERT(a);
    TEST_ASSERT(a1);
  }
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1{std::move(a)};
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1{std::move(a)};
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
}

void test_ptrAssignment() {
  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1{};
    a1 = a;
    TEST_ASSERT(a);
    TEST_ASSERT(a1);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);

  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1;
    a1 = std::move(a);
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
}

void test_ptrAccess() {
  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    a->x = 1;
    A& aref = *a;
    aref.x = 2;
    TEST_ASSERT(!!a);
    TEST_ASSERT_EQUAL(a->x, (*a).x);
  }
  {
    RcPtr<A> const a{MakeRcPtr<A>()};
    a->x = 1;
    A& aref = *a;
    aref.x = 2;
    TEST_ASSERT(!!a);
    TEST_ASSERT_EQUAL(a->x, (*a).x);
  }
}

void test_ptrLifeTime() {
  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtr<A> a1{MakeRcPtr<A>()};
  }
  TEST_ASSERT_EQUAL(2, A::a_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    // check if parent destructor is called too
    RcPtr<B> b{MakeRcPtr<B>()};
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(1, B::b_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
}

void test_ptrView() {
  {
    RcPtrView<A> a_view{};
    TEST_ASSERT(!a_view);
  }
  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtrView<A> a_view{a};
    TEST_ASSERT(!!a_view);
    a.Reset();
    TEST_ASSERT(!a_view);
    TEST_ASSERT_EQUAL(1, A::a_destroyed);
  }

  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtrView<A> a_view{a};
    auto a1 = a_view.lock();
    TEST_ASSERT(!!a1);
    a.Reset();
    TEST_ASSERT_EQUAL(0, A::a_destroyed);
    a1.Reset();
    TEST_ASSERT_EQUAL(1, A::a_destroyed);
    auto a2 = a_view.lock();
    TEST_ASSERT(!a2);
  }

  A::a_destroyed = 0;
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    std::vector<RcPtrView<A>> views;
    for (std::size_t i = 0; i < 10; ++i) {
      views.emplace_back(a);
    }
    for (auto& v : views) {
      v.Reset();
    }
    TEST_ASSERT_EQUAL(0, A::a_destroyed);
    a.Reset();
    TEST_ASSERT_EQUAL(1, A::a_destroyed);
  }
  {
    RcPtr<A> a{MakeRcPtr<A>()};
    RcPtrView<A> a_view;
    TEST_ASSERT(!a_view);
    a_view = a;
    TEST_ASSERT(!!a_view);
    RcPtrView<A> a_view1{std::move(a_view)};
    TEST_ASSERT(!a_view);
    TEST_ASSERT(!!a_view1);
    RcPtrView<A> a_view2;
    TEST_ASSERT(!a_view2);
    a_view2 = std::move(a_view1);
    TEST_ASSERT(!a_view1);
    TEST_ASSERT(!!a_view2);
  }
}

}  // namespace ae::test_rc_ptr

int test_rc_ptr() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_rc_ptr::test_createPtr);
  RUN_TEST(ae::test_rc_ptr::test_ptrAssignment);
  RUN_TEST(ae::test_rc_ptr::test_ptrAccess);
  RUN_TEST(ae::test_rc_ptr::test_ptrLifeTime);
  RUN_TEST(ae::test_rc_ptr::test_ptrView);
  return UNITY_END();
}
