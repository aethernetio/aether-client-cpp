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

#include "tests/test-ptr/pipe_there_they_are_sitting.h"

namespace ae::test_ptr {
using test_ptr::A;
using test_ptr::B;

void test_createPtr() {
  {
    Ptr<A> a;
    Ptr<A> a1{nullptr};
  }
  {
    Ptr<A> a{MakePtr<A>()};
    TEST_ASSERT(a);
  }
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1{a};
    TEST_ASSERT(a);
    TEST_ASSERT(a1);
  }
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1{std::move(a)};
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1{std::move(a)};
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
  {
    Ptr<A> a{MakePtr<B>()};
    TEST_ASSERT(a);
  }
  {
    Ptr<B> b{MakePtr<B>()};
    Ptr<A> a{b};
    TEST_ASSERT(b);
    TEST_ASSERT(a);
  }
  {
    Ptr<B> b{MakePtr<B>()};
    Ptr<A> a{std::move(b)};
    TEST_ASSERT(!b);
    TEST_ASSERT(a);
  }
}

void test_ptrLifeTime() {
  A::a_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1{MakePtr<A>()};
  }
  TEST_ASSERT_EQUAL(2, A::a_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    // check if parent destructor is called too
    Ptr<B> b{MakePtr<B>()};
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(1, B::b_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    // check if parent destructor is called too
    Ptr<A> b{MakePtr<B>()};
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(1, B::b_destroyed);
}

void test_ptrAssignment() {
  A::a_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1{};
    a1 = a;
    TEST_ASSERT(a);
    TEST_ASSERT(a1);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);

  A::a_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<A> a1;
    a1 = std::move(a);
    TEST_ASSERT(!a);
    TEST_ASSERT(a1);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<B> b = a;
    TEST_ASSERT(a);
    TEST_ASSERT(b);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(0, B::b_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<B> b{};
    b = a;
    TEST_ASSERT(a);
    TEST_ASSERT(b);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(0, B::b_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<B> b{};
    b = std::move(a);
    TEST_ASSERT(!a);
    TEST_ASSERT(b);
  }
  TEST_ASSERT_EQUAL(1, A::a_destroyed);
  TEST_ASSERT_EQUAL(0, B::b_destroyed);

  A::a_destroyed = 0;
  B::b_destroyed = 0;
  {
    Ptr<A> a{MakePtr<A>()};
    Ptr<B> b{MakePtr<B>()};
    b = std::move(a);
    TEST_ASSERT(!a);
    TEST_ASSERT(b);
  }
  TEST_ASSERT_EQUAL(2, A::a_destroyed);
  TEST_ASSERT_EQUAL(1, B::b_destroyed);
}

void test_ptrCompare() {
  {
    Ptr<A> a{MakePtr<A>()};
    TEST_ASSERT(a);
    Ptr<A> a1{MakePtr<A>()};
    TEST_ASSERT(a1);
    TEST_ASSERT(a != a1);
  }

  {
    Ptr<A> a{MakePtr<A>()};
    TEST_ASSERT(a);
    Ptr<A> a1 = a;
    TEST_ASSERT(a1);
    TEST_ASSERT(a == a1);
  }

  {
    Ptr<A> a;
    TEST_ASSERT(!a);
    Ptr<A> a1 = a;
    TEST_ASSERT(!a1);
  }

  {
    Ptr<A> a{MakePtr<A>()};
    TEST_ASSERT(a);
    Ptr<B> b = a;
    TEST_ASSERT(a == b);
  }

  {
    Ptr<A> a{MakePtr<A>()};
    TEST_ASSERT(a);
    Ptr<B> b{MakePtr<B>()};
    TEST_ASSERT(a != b);
  }
}

struct MakeYourSelf {
  MakeYourSelf() {
    auto self_ptr = MakePtrFromThis(this);
    TEST_ASSERT_EQUAL(this, self_ptr.get());
    a = MakePtr<A>();
    x = 12;
  }

  Ptr<A> a;
  int x;
};

void test_ptrMakeFromThis() {
  {
    auto a = MakePtr<A>();
    auto* a_this = a.get();
    auto a_copy = MakePtrFromThis(a_this);
    TEST_ASSERT_EQUAL(a.get(), a_copy.get());
  }
  {
    auto mys = MakePtr<MakeYourSelf>();
    TEST_ASSERT(!!mys);
    TEST_ASSERT(!!mys->a);
    TEST_ASSERT_EQUAL(12, mys->x);
  }
}

}  // namespace ae::test_ptr

int run_test_ptr() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ptr::test_createPtr);
  RUN_TEST(ae::test_ptr::test_ptrLifeTime);
  RUN_TEST(ae::test_ptr::test_ptrAssignment);
  RUN_TEST(ae::test_ptr::test_ptrCompare);
  RUN_TEST(ae::test_ptr::test_ptrMakeFromThis);
  return UNITY_END();
}
