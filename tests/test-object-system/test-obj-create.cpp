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
    Foo::ptr foo = Foo::ptr::Create(CreateWith{domain}.with_id(1));

    // p is a loaded object
    foo.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      p->bar.WithLoaded([](auto const& p_bar) { TEST_ASSERT(p_bar); });
    });

    foo.Save();
    TEST_ASSERT(facility.map.find(foo.id().id()) != facility.map.end());
    TEST_ASSERT(facility.map.find(foo->bar.id().id()) != facility.map.end());

    // load object for already loaded list
    Foo::ptr foo2 = Foo::ptr::Declare(CreateWith{domain}.with_id(1));
    TEST_ASSERT(foo2.is_valid());
    TEST_ASSERT_FALSE(foo2.is_loaded());

    foo2.Load();
    TEST_ASSERT(foo2);
    TEST_ASSERT(foo2->bar);
    TEST_ASSERT_EQUAL(foo2.id().id(), foo.id().id());

    // load object from new domain
    Foo::ptr foo3 = Foo::ptr::Declare(CreateWith{domain2}.with_id(1));
    TEST_ASSERT(foo3.is_valid());
    TEST_ASSERT_FALSE(foo3.is_loaded());

    foo3.WithLoaded([](auto const p3) {
      TEST_ASSERT(p3);
      TEST_ASSERT(p3->bar);
    });
    TEST_ASSERT_EQUAL(foo3.id().id(), foo.id().id());
  }
}

void test_createBob() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    Bob::ptr bob = Bob::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(bob);
    TEST_ASSERT(bob->foo_prefab);
    bob.Save();
  }
  Bob::ptr bob = Bob::ptr::Declare(CreateWith(domain).with_id(1));
  bob.Load();
  TEST_ASSERT(bob);
  TEST_ASSERT(bob->foo_prefab.is_valid());
  TEST_ASSERT(!bob->foo_prefab.is_loaded());
  auto foo = bob->CreateFoo();
  TEST_ASSERT(foo);
  TEST_ASSERT(foo->bar);
  auto foo2 = bob->CreateFoo();
  TEST_ASSERT(foo2);
  TEST_ASSERT(foo2->bar);
  // it's different copies
  TEST_ASSERT(foo2.id() != foo.id());
  TEST_ASSERT(foo2.Load() != foo.Load());
  // but internal the same
  TEST_ASSERT(foo2->bar.Load() == foo->bar.Load());
  // foo is registered and same id loads same object
  Foo::ptr foo3 = Foo::ptr::Declare(CreateWith{domain}.with_id(foo.id()));
  foo3.Load();
  TEST_ASSERT(foo3);
  TEST_ASSERT(foo3.Load() == foo.Load());
}

void test_cloneFoo() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  auto foo_prefab = Foo::ptr::Create(CreateWith{domain}.with_id(100));
  TEST_ASSERT(foo_prefab);
  foo_prefab.Save();

  auto foo1 = foo_prefab.Clone(1);
  TEST_ASSERT(foo1);
  TEST_ASSERT_EQUAL(1, foo1.id().id());
  TEST_ASSERT(foo1.id() == foo1->obj_id);

  foo_prefab.Reset();
  TEST_ASSERT(foo_prefab.is_valid());
  TEST_ASSERT(!foo_prefab.is_loaded());

  auto foo2 = foo_prefab.Clone(2);
  TEST_ASSERT(foo2);
  TEST_ASSERT_EQUAL(2, foo2.id().id());
  TEST_ASSERT(foo2.id() == foo2->obj_id);
}

void test_createBobsMother() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    BobsMother::ptr bobs_mother =
        BobsMother::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(bobs_mother);
    TEST_ASSERT(bobs_mother->bob_prefab);
    TEST_ASSERT(bobs_mother->bob_prefab->foo_prefab);
    bobs_mother.Save();
  }
  BobsMother::ptr bobs_mother =
      BobsMother::ptr::Declare(CreateWith{domain}.with_id(1));
  bobs_mother.Load();
  TEST_ASSERT(bobs_mother);
  TEST_ASSERT(!bobs_mother->bob_prefab.is_loaded());
  auto bob = bobs_mother->CreateBob();
  TEST_ASSERT(bob);
  TEST_ASSERT(!bob->foo_prefab.is_loaded());
  auto foo = bob->CreateFoo();
  TEST_ASSERT(foo);
}

void test_createBobsFather() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  {
    BobsFather::ptr bobs_father =
        BobsFather::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(bobs_father);
    TEST_ASSERT(!bobs_father->GetBob());
    bobs_father.Save();
  }
  {
    BobsFather::ptr bobs_father =
        BobsFather::ptr::Declare(CreateWith{domain}.with_id(1));
    bobs_father.Load();
    TEST_ASSERT(bobs_father);
    TEST_ASSERT(!bobs_father->GetBob());
    bobs_father->SetBob(Bob::ptr::Create(CreateWith{domain}.with_id(2)));
    bobs_father.Save();
  }
  BobsFather::ptr bobs_father =
      BobsFather::ptr::Declare(CreateWith{domain}.with_id(1));
  bobs_father.Load();
  TEST_ASSERT(bobs_father);
  TEST_ASSERT(bobs_father->GetBob());
}

void test_createCollector() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  {
    Collector::ptr collector =
        Collector::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(collector);
    collector.Save();
  }
  Collector::ptr collector =
      Collector::ptr::Declare(CreateWith{domain}.with_id(1));
  collector.Load();
  TEST_ASSERT(collector);
  TEST_ASSERT(collector->vec_bars.size() == Collector::kSize);
  TEST_ASSERT(collector->list_bars.size() == Collector::kSize);
  TEST_ASSERT(collector->map_bars.size() == Collector::kSize);
  for (auto i = 0; i < Collector::kSize; i++) {
    TEST_ASSERT(collector->vec_bars[i]);
    TEST_ASSERT_EQUAL_FLOAT(3.2, collector->vec_bars[i]->y);
  }
  for (auto& bar : collector->list_bars) {
    TEST_ASSERT(bar);
    TEST_ASSERT_EQUAL_FLOAT(3.2, bar->y);
  }
  for (auto& [i, bar] : collector->map_bars) {
    TEST_ASSERT(!bar);
    bar.Load();
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
    Poopa::ptr poopa = Poopa::ptr::Create(CreateWith{domain}.with_id(1));
    Loopa::ptr loopa = Loopa::ptr::Create(CreateWith{domain}.with_id(2));
    TEST_ASSERT(poopa);
    TEST_ASSERT(loopa);

    poopa->SetLoopa(loopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    poopa.Save();
    loopa.Save();
    TEST_MESSAGE("Poopa and Loopa saved");
    loopa.Reset();
    poopa.Reset();
  }
  TEST_ASSERT_EQUAL(1, Poopa::DeleteCount);
  TEST_ASSERT_EQUAL(1, Loopa::DeleteCount);
  Poopa::ptr poopa = Poopa::ptr::Declare(CreateWith{domain}.with_id(1));
  poopa.Load();
  TEST_ASSERT(poopa);
  TEST_ASSERT(poopa->loopa);

  Loopa::ptr loopa = Loopa::ptr::Declare(CreateWith{domain}.with_id(2));
  loopa.Load();
  TEST_ASSERT(poopa);
  TEST_ASSERT(poopa->loopa);

  TEST_ASSERT(poopa->loopa.Load() == loopa.Load());
  for (auto& p : loopa->poopas) {
    TEST_ASSERT(poopa.Load() == p.Load());
  }
}

void test_cyclePoopaLoopaReverse() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};

  Poopa::DeleteCount = 0;
  Loopa::DeleteCount = 0;
  {
    Loopa::ptr loopa = Loopa::ptr::Create(CreateWith{domain}.with_id(2));
    Poopa::ptr poopa = Poopa::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(loopa);
    TEST_ASSERT(poopa);

    poopa->SetLoopa(loopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    loopa->AddPoopa(poopa);
    poopa.Reset();
    TEST_ASSERT_EQUAL(0, Poopa::DeleteCount);

    loopa.Save();
    TEST_MESSAGE("Loopa saved");
  }
  TEST_ASSERT_EQUAL(1, Poopa::DeleteCount);
  TEST_ASSERT_EQUAL(1, Loopa::DeleteCount);

  Loopa::ptr loopa = Loopa::ptr::Declare(CreateWith{domain}.with_id(2));
  loopa.Load();

  for (auto& p : loopa->poopas) {
    TEST_ASSERT(p);
    auto poopa = static_cast<ObjPtr<Poopa>>(p);
    TEST_ASSERT(poopa->loopa.Load() == loopa.Load());
  }
}

void test_Family() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  // create child and test is father and obj saved too
  {
    Child::ptr child = Child::ptr::Create(CreateWith{domain}.with_id(1));
    TEST_ASSERT(child);
    child.Save();
    TEST_ASSERT(facility.map.find(child.id().id()) != facility.map.end());

    auto& classes = facility.map[child.id().id()];
    TEST_ASSERT(classes.find(Child::kClassId) != classes.end());
    TEST_ASSERT_EQUAL(0, classes[Child::kClassId][0]->size());
    TEST_ASSERT(classes.find(Father::kClassId) != classes.end());
    TEST_ASSERT_EQUAL(0, classes[Father::kClassId][0]->size());
    TEST_ASSERT(classes.find(Obj::kClassId) != classes.end());
  }
  {
    Child::ptr child = Child::ptr::Declare(CreateWith{domain}.with_id(1));
    child.Load();
    TEST_ASSERT(child);
  }
}
}  // namespace ae::test_obj_create

int run_test_object_create() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_obj_create::test_createFoo);
  RUN_TEST(ae::test_obj_create::test_createBob);
  RUN_TEST(ae::test_obj_create::test_cloneFoo);
  RUN_TEST(ae::test_obj_create::test_createBobsMother);
  RUN_TEST(ae::test_obj_create::test_createBobsFather);
  RUN_TEST(ae::test_obj_create::test_createCollector);
  RUN_TEST(ae::test_obj_create::test_cyclePoopaLoopa);
  RUN_TEST(ae::test_obj_create::test_cyclePoopaLoopaReverse);
  RUN_TEST(ae::test_obj_create::test_Family);
  return UNITY_END();
}
