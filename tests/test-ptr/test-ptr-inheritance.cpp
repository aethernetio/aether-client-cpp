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
#include "unity_internals.h"

namespace ae::test_ptr_inheritance {
struct BaseA {
  virtual ~BaseA() = default;

  virtual int calculateA() = 0;
};

struct BaseB {
  virtual ~BaseB() = default;

  virtual int calculateB() = 0;
};

struct DerivedAB : public BaseA, public BaseB {
  static inline int dab_destroyed = 0;

  ~DerivedAB() override { ++dab_destroyed; }
  int calculateA() override { return 1; }
  int calculateB() override { return 2; }
};

void test_ptrInheritance() {
  DerivedAB::dab_destroyed = 0;
  {
    auto dab = MakePtr<DerivedAB>();
    auto da = Ptr<BaseA>(dab);
    auto db = Ptr<BaseB>(dab);

    TEST_ASSERT_EQUAL(1, da->calculateA());
    TEST_ASSERT_EQUAL(2, db->calculateB());
  }
  TEST_ASSERT_EQUAL(1, DerivedAB::dab_destroyed);

  DerivedAB::dab_destroyed = 0;
  {
    Ptr<BaseA> da = MakePtr<DerivedAB>();
    TEST_ASSERT_EQUAL(1, da->calculateA());
  }
  TEST_ASSERT_EQUAL(1, DerivedAB::dab_destroyed);

  DerivedAB::dab_destroyed = 0;
  {
    Ptr<BaseB> db = MakePtr<DerivedAB>();
    TEST_ASSERT_EQUAL(2, db->calculateB());
  }
  TEST_ASSERT_EQUAL(1, DerivedAB::dab_destroyed);
}

}  // namespace ae::test_ptr_inheritance

int test_ptr_inheritance() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ptr_inheritance::test_ptrInheritance);
  return UNITY_END();
}
