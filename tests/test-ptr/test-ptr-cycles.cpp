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

#include "tests/sanitizer_checks.h"

namespace ae {
namespace test_ptr_cycles {
struct ObjB;
struct ObjC;
struct ObjA {
  ~ObjA() { ++obj_a_destroyed; }
  static inline int obj_a_destroyed = 0;
  Ptr<ObjB> obj_b;
  Ptr<ObjC> obj_c;

  AE_CLASS_REFLECT(obj_b, obj_c)
};

struct ObjB {
  ~ObjB() { ++obj_b_destroyed; }
  static inline int obj_b_destroyed = 0;
  Ptr<ObjA> obj_a;

  AE_CLASS_REFLECT(obj_a)
};

struct ObjC {
  ~ObjC() { ++obj_c_destroyed; }
  static inline int obj_c_destroyed = 0;

  AE_CLASS_REFLECT()
};

void test_ObjAnObjBCycleRef() {
  ObjA::obj_a_destroyed = 0;
  ObjB::obj_b_destroyed = 0;
  {
    auto a = MakePtr<ObjA>();
    auto b = MakePtr<ObjB>();
    a->obj_b = b;
    b->obj_a = a;
  }
  // a and b are destroyed
  TEST_ASSERT_EQUAL(1, ObjA::obj_a_destroyed);
  TEST_ASSERT_EQUAL(1, ObjB::obj_b_destroyed);
}

void test_ObjAnDoubleObjBCycleRef() {
  ObjA::obj_a_destroyed = 0;
  ObjB::obj_b_destroyed = 0;
  {
    auto a = MakePtr<ObjA>();
    auto b1 = MakePtr<ObjB>();
    a->obj_b = b1;
    b1->obj_a = a;

    auto b2 = MakePtr<ObjB>();
    b2->obj_a = a;
    auto b2_copy = b2;
  }
  // a and b are destroyed
  TEST_ASSERT_EQUAL(1, ObjA::obj_a_destroyed);
  TEST_ASSERT_EQUAL(2, ObjB::obj_b_destroyed);
}

void test_ObjAnObjBnObjCCycleRef() {
  ObjA::obj_a_destroyed = 0;
  ObjB::obj_b_destroyed = 0;
  ObjC::obj_c_destroyed = 0;
  {
    auto c = MakePtr<ObjC>();
    auto a = MakePtr<ObjA>();
    auto b = MakePtr<ObjB>();
    a->obj_b = b;
    a->obj_c = c;
    b->obj_a = a;
    auto c_copy1 = c;
    auto c_copy2 = c;
    auto c_copy3 = c;
  }
  TEST_ASSERT_EQUAL(1, ObjA::obj_a_destroyed);
  TEST_ASSERT_EQUAL(1, ObjB::obj_b_destroyed);
  TEST_ASSERT_EQUAL(1, ObjC::obj_c_destroyed);
}

void test_ObjAnObjBWithPtrVew() {
  ObjA::obj_a_destroyed = 0;
  ObjB::obj_b_destroyed = 0;
  {
    auto a = MakePtr<ObjA>();
    auto b = MakePtr<ObjB>();
    a->obj_b = b;
    b->obj_a = a;
    PtrView b_view = b;
    PtrView a_view = a;
  }
  TEST_ASSERT_EQUAL(1, ObjA::obj_a_destroyed);
  TEST_ASSERT_EQUAL(1, ObjB::obj_b_destroyed);
}

struct ObjE;
struct ObjD {
  ~ObjD() { ++obj_d_destroyed; }
  static inline int obj_d_destroyed = 0;
  Ptr<ObjE> ptr_e;

  AE_CLASS_REFLECT(ptr_e)
};

struct ObjE {
  ~ObjE() { ++obj_e_destroyed; }
  static inline int obj_e_destroyed = 0;
  PtrView<ObjD> ptr_d;

  AE_CLASS_REFLECT(ptr_d)
};

void test_ObjDnObjECycleWithPtrView() {
  ObjD::obj_d_destroyed = 0;
  ObjE::obj_e_destroyed = 0;
  {
    auto d = MakePtr<ObjD>();
    auto e = MakePtr<ObjE>();
    d->ptr_e = e;
    e->ptr_d = d;
  }
  TEST_ASSERT_EQUAL(1, ObjD::obj_d_destroyed);
  TEST_ASSERT_EQUAL(1, ObjE::obj_e_destroyed);
}

struct ObjG;
struct ObjF {
  ~ObjF() { ++obj_f_destroyed; }
  static inline int obj_f_destroyed = 0;
  std::vector<Ptr<ObjG>> g_list;

  AE_CLASS_REFLECT(g_list)
};

struct ObjG {
  static inline int obj_g_destroyed = 0;
  ~ObjG() { ++obj_g_destroyed; }

  Ptr<ObjF> obj_f;

  AE_CLASS_REFLECT(obj_f)
};

void test_CycleListPtr() {
  ObjF::obj_f_destroyed = 0;
  ObjG::obj_g_destroyed = 0;
  {
    auto f = MakePtr<ObjF>();
    {
      auto g = MakePtr<ObjG>();
      g->obj_f = f;
      f->g_list.push_back(g);
      f->g_list.push_back(g);
      f->g_list.push_back(g);
    }
    TEST_ASSERT_EQUAL(0, ObjG::obj_g_destroyed);
  }
  TEST_ASSERT_EQUAL(1, ObjF::obj_f_destroyed);
  TEST_ASSERT_EQUAL(1, ObjG::obj_g_destroyed);
}
}  // namespace test_ptr_cycles
}  // namespace ae

int test_ptr_cycles() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ptr_cycles::test_ObjAnObjBCycleRef);
  RUN_TEST(ae::test_ptr_cycles::test_ObjAnDoubleObjBCycleRef);
  RUN_TEST(ae::test_ptr_cycles::test_ObjAnObjBnObjCCycleRef);
  RUN_TEST(ae::test_ptr_cycles::test_ObjAnObjBWithPtrVew);
  RUN_TEST(ae::test_ptr_cycles::test_ObjDnObjECycleWithPtrView);
  RUN_TEST(ae::test_ptr_cycles::test_CycleListPtr);
  return UNITY_END();
}
