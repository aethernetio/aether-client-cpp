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

#include "aether/obj/domain.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"
#include "objects/foo.h"
#include "objects/bob.h"
#include "objects/bar.h"
#include "objects/poopa_loopa.h"
#include "objects/collector.h"
#include "objects/family.h"

#include "map_domain_storage.h"

namespace ae::test_obj_create {

void test_createFoo() {
  // create objects
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  Domain domain2{ae::ClockType::now(), facility};
  {
    Foo::ptr foo = domain.CreateObj<Foo>(1);

    TEST_ASSERT(foo);
    TEST_ASSERT(foo->bar);

    domain.SaveRoot(foo);
    TEST_ASSERT(facility.map.find(foo.GetId().id()) != facility.map.end());
    TEST_ASSERT(facility.map.find(foo->bar.GetId().id()) != facility.map.end());

    // load object for already loaded list
    Foo::ptr foo2;
    foo2.SetId(1);
    domain.LoadRoot(foo2);

    TEST_ASSERT(foo2);
    TEST_ASSERT(foo2->bar);
    TEST_ASSERT_EQUAL(foo2.GetId().id(), foo.GetId().id());

    // load object from new domain
    Foo::ptr foo3;
    foo3.SetId(1);
    domain2.LoadRoot(foo3);

    TEST_ASSERT(foo3);
    TEST_ASSERT(foo3->bar);

    TEST_ASSERT_EQUAL(foo3.GetId().id(), foo.GetId().id());
  }
}

void test_createBob() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    Bob::ptr bob = domain.CreateObj<Bob>(1);
    TEST_ASSERT(bob);
    TEST_ASSERT(bob->foo_prefab);
    domain.SaveRoot(bob);
  }
  Bob::ptr bob{};
  bob.SetId(1);
  domain.LoadRoot(bob);
  TEST_ASSERT(bob);
  TEST_ASSERT(!bob->foo_prefab);
  TEST_ASSERT(bob->foo_prefab.GetId().IsValid());
  auto foo = bob->CreateFoo();
  TEST_ASSERT(foo);
  TEST_ASSERT(foo->bar);
  auto foo2 = bob->CreateFoo();
  TEST_ASSERT(foo2);
  // it's different copies
  TEST_ASSERT(foo2->GetId() != foo->GetId());
  TEST_ASSERT(foo2 != foo);
  // but internal the same
  TEST_ASSERT(foo2->bar == foo->bar);
  // foo is registered and same id loads same object
  Foo::ptr foo3;
  foo3.SetId(foo->GetId());
  domain.LoadRoot(foo3);
  TEST_ASSERT(foo3);
  TEST_ASSERT(foo3 == foo);
}

void test_createBobsMother() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    BobsMother::ptr bobs_mother = domain.CreateObj<BobsMother>(1);
    TEST_ASSERT(bobs_mother);
    TEST_ASSERT(bobs_mother->bob_prefab);
    TEST_ASSERT(bobs_mother->bob_prefab->foo_prefab);
    domain.SaveRoot(bobs_mother);
  }
  BobsMother::ptr bobs_mother{};
  bobs_mother.SetId(1);
  domain.LoadRoot(bobs_mother);
  TEST_ASSERT(bobs_mother);
  TEST_ASSERT(!bobs_mother->bob_prefab);
  auto bob = bobs_mother->CreateBob();
  TEST_ASSERT(bob);
  TEST_ASSERT(!bob->foo_prefab);
  auto foo = bob->CreateFoo();
  TEST_ASSERT(foo);
}

void test_createBobsFather() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    BobsFather::ptr bobs_father = domain.CreateObj<BobsFather>(1);
    TEST_ASSERT(bobs_father);
    TEST_ASSERT(!bobs_father->GetBob());
    domain.SaveRoot(bobs_father);
  }
  {
    BobsFather::ptr bobs_father{};
    bobs_father.SetId(1);
    domain.LoadRoot(bobs_father);
    TEST_ASSERT(bobs_father);
    TEST_ASSERT(!bobs_father->GetBob());
    bobs_father->SetBob(domain.CreateObj<Bob>(2));
    domain.SaveRoot(bobs_father);
  }
  BobsFather::ptr bobs_father{};
  bobs_father.SetId(1);
  domain.LoadRoot(bobs_father);
  TEST_ASSERT(bobs_father);
  TEST_ASSERT(bobs_father->GetBob());
}

void test_createCollector() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  {
    Collector::ptr collector = domain.CreateObj<Collector>(1);
    TEST_ASSERT(collector);
    domain.SaveRoot(collector);
  }
  Collector::ptr collector{};
  collector.SetId(1);
  domain.LoadRoot(collector);
  TEST_ASSERT(collector);
  TEST_ASSERT(collector->vec_bars.size() == Collector::SIZE);
  TEST_ASSERT(collector->list_bars.size() == Collector::SIZE);
  TEST_ASSERT(collector->map_bars.size() == Collector::SIZE);
  for (auto i = 0; i < Collector::SIZE; i++) {
    TEST_ASSERT(collector->vec_bars[i]);
    TEST_ASSERT_EQUAL_FLOAT(3.2, collector->vec_bars[i]->y);
  }
  for (auto& bar : collector->list_bars) {
    TEST_ASSERT(bar);
    TEST_ASSERT_EQUAL_FLOAT(3.2, bar->y);
  }
  for (auto& [i, bar] : collector->map_bars) {
    TEST_ASSERT(!bar);
    domain.LoadRoot(bar);
    TEST_ASSERT(bar);
    TEST_ASSERT_EQUAL_FLOAT(3.2, bar->y);
  }
}

void test_cyclePoopaLoopa() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  Poopa::DeleteCount = 0;
  Loopa::DeleteCount = 0;
  {
    Poopa::ptr poopa = domain.CreateObj<Poopa>(1);
    Loopa::ptr loopa = domain.CreateObj<Loopa>(2);
    TEST_ASSERT(poopa);
    TEST_ASSERT(loopa);

    poopa->SetLoopa(loopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    domain.SaveRoot(poopa);
    domain.SaveRoot(loopa);
    TEST_MESSAGE("Poopa and Loopa saved");
  }
  TEST_ASSERT_EQUAL(1, Poopa::DeleteCount);
  TEST_ASSERT_EQUAL(1, Loopa::DeleteCount);
  Poopa::ptr poopa{};
  poopa.SetId(1);
  domain.LoadRoot(poopa);
  TEST_ASSERT(poopa);
  TEST_ASSERT(poopa->loopa);

  Loopa::ptr loopa{};
  loopa.SetId(2);
  domain.LoadRoot(loopa);
  TEST_ASSERT(poopa);
  TEST_ASSERT(poopa->loopa);

  TEST_ASSERT(poopa->loopa == loopa);
  for (auto& p : loopa->poopas) {
    TEST_ASSERT(poopa == p);
  }
}

void test_cyclePoopaLoopaReverse() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  Poopa::DeleteCount = 0;
  Loopa::DeleteCount = 0;
  {
    Loopa::ptr loopa = domain.CreateObj<Loopa>(2);
    Poopa::ptr poopa = domain.CreateObj<Poopa>(1);
    TEST_ASSERT(loopa);
    TEST_ASSERT(poopa);

    poopa->SetLoopa(loopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    poopa.Reset();
    TEST_ASSERT_EQUAL(0, Poopa::DeleteCount);

    domain.SaveRoot(loopa);
    TEST_MESSAGE("Loopa saved");
  }
  TEST_ASSERT_EQUAL(1, Poopa::DeleteCount);
  TEST_ASSERT_EQUAL(1, Loopa::DeleteCount);

  Loopa::ptr loopa{};
  loopa.SetId(2);
  domain.LoadRoot(loopa);

  for (auto& p : loopa->poopas) {
    TEST_ASSERT(p);
    auto poopa = static_cast<ObjPtr<Poopa>>(p);
    TEST_ASSERT(poopa->loopa == loopa);
  }
}

void test_Family() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  // create child and test is father and obj saved too
  {
    Child::ptr child = domain.CreateObj<Child>(1);
    TEST_ASSERT(child);
    domain.SaveRoot(child);
    TEST_ASSERT(facility.map.find(child.GetId().id()) != facility.map.end());

    auto& classes = facility.map[child->GetId().id()];
    TEST_ASSERT(classes.find(Child::kClassId) != classes.end());
    TEST_ASSERT_EQUAL(0, classes[Child::kClassId][0]->size());
    TEST_ASSERT(classes.find(Father::kClassId) != classes.end());
    TEST_ASSERT_EQUAL(0, classes[Father::kClassId][0]->size());
    TEST_ASSERT(classes.find(Obj::kClassId) != classes.end());
  }
  {
    Child::ptr child;
    child.SetId(1);
    domain.LoadRoot(child);
    TEST_ASSERT(child);
  }
}
}  // namespace ae::test_obj_create

int run_test_object_create() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_obj_create::test_createFoo);
  RUN_TEST(ae::test_obj_create::test_createBob);
  RUN_TEST(ae::test_obj_create::test_createBobsMother);
  RUN_TEST(ae::test_obj_create::test_createBobsFather);
  RUN_TEST(ae::test_obj_create::test_createCollector);
  RUN_TEST(ae::test_obj_create::test_cyclePoopaLoopa);
  RUN_TEST(ae::test_obj_create::test_cyclePoopaLoopaReverse);
  RUN_TEST(ae::test_obj_create::test_Family);
  return UNITY_END();
}
