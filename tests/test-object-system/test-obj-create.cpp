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

#include <type_traits>
#include <utility>

#include "aether/obj/domain.h"
#include "aether/obj/obj_ptr.h"
#include "aether/obj/registry.h"
#include "objects/bar.h"
#include "objects/bob.h"
#include "objects/collector.h"
#include "objects/family.h"
#include "objects/foo.h"
#include "objects/poopa_loopa.h"

#include "map_domain_storage.h"

namespace ae::test_obj_create {

namespace test_obj_create_internal {

void AssertEmptyPtr(Foo::ptr const& ptr) {
  TEST_ASSERT_FALSE(ptr.is_valid());
  TEST_ASSERT_FALSE(ptr.is_loaded());
  TEST_ASSERT_FALSE(ptr.Load());
}

void AssertCopiedPtr(Foo::ptr const& foo, Foo::ptr const& copy,
                     Foo::ptr const& assigned) {
  TEST_ASSERT(copy.is_loaded());
  TEST_ASSERT(copy.Load().get() == foo.Load().get());
  TEST_ASSERT(assigned.Load().get() == foo.Load().get());
}

void AssertMovedPtr(Foo::ptr const& foo, Foo::ptr const& moved) {
  TEST_ASSERT(moved.Load().get() == foo.Load().get());
}

void AssertMovedFromPtr(Foo::ptr const& foo, Foo::ptr const& ptr) {
  TEST_ASSERT_FALSE(ptr.is_loaded());
  TEST_ASSERT(ptr.is_valid());
  TEST_ASSERT_EQUAL(foo.id().id(), ptr.id().id());
  TEST_ASSERT_EQUAL_PTR(foo.domain(), ptr.domain());
  TEST_ASSERT_EQUAL(static_cast<ObjFlags::Type>(foo.flags()),
                    static_cast<ObjFlags::Type>(ptr.flags()));
}

void TestEmptyAndUnloadedPtrOwnership(Domain& domain) {
  Foo::ptr const empty;
  AssertEmptyPtr(empty);
  auto empty_copy = empty;
  Foo::ptr empty_assigned;
  empty_assigned = empty_copy;
  auto empty_moved = std::move(empty_copy);
  empty_assigned = std::move(empty_moved);
  AssertEmptyPtr(empty_assigned);

  constexpr auto kUnloadedObjectId = 10;
  auto unloaded =
      Foo::ptr::Declare(CreateWith{domain}.with_id(kUnloadedObjectId));
  auto unloaded_copy = unloaded;
  Foo::ptr unloaded_assigned;
  unloaded_assigned = unloaded_copy;
  auto unloaded_moved = std::move(unloaded_copy);
  unloaded_assigned = std::move(unloaded_moved);
  TEST_ASSERT(unloaded_assigned.is_valid());
  TEST_ASSERT_FALSE(unloaded_assigned.is_loaded());
}

Foo::ptr CreateCachedFoo(Domain& domain) {
  return Foo::ptr::Create(CreateWith{domain}.with_id(1));
}

void AssertCachedBaseView(Foo::ptr const& foo) {
  auto const& base = static_cast<ObjectPtrBase const&>(foo);
  TEST_ASSERT(base.is_valid());
  TEST_ASSERT(base.is_loaded());
  TEST_ASSERT(base.LoadCached().get() == foo.Load().get());
}

void AssertCachedCopyAndMoveOwnership(Foo::ptr const& foo, Foo::ptr& moved) {
  auto copy = foo;

  Foo::ptr assigned;
  assigned = copy;
  AssertCopiedPtr(foo, copy, assigned);

  moved = std::move(copy);
  AssertMovedPtr(foo, moved);
  // ObjectPtr preserves identity while releasing its cached object after move.
  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  AssertMovedFromPtr(foo, copy);

  moved = std::move(moved);
  AssertMovedPtr(foo, moved);
}

void AssertCachedMoveAssignmentOwnership(Domain& domain, Foo::ptr& foo,
                                         Foo::ptr& moved) {
  auto replacement = Foo::ptr::Create(CreateWith{domain}.with_id(2));
  Ptr<Foo> const retained_replacement = replacement.Load();
  replacement = std::move(moved);
  TEST_ASSERT(replacement.Load().get() == foo.Load().get());
  TEST_ASSERT(retained_replacement);
  // Move assignment transfers the source cache, never the destination cache.
  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  AssertMovedFromPtr(foo, moved);
  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  TEST_ASSERT(moved.Load().get() == foo.Load().get());

  Ptr<Foo> const retained = replacement.Load();
  auto& base = static_cast<ObjectPtrBase&>(foo);
  base.Reset();
  TEST_ASSERT_FALSE(foo.is_loaded());
  replacement.Reset();
  TEST_ASSERT(retained);
}

void TestCachedPtrOwnership(Domain& domain) {
  auto foo = CreateCachedFoo(domain);
  AssertCachedBaseView(foo);
  auto moved = Foo::ptr{};
  AssertCachedCopyAndMoveOwnership(foo, moved);
  AssertCachedMoveAssignmentOwnership(domain, foo, moved);
}

void TestEmptyAndUnloadedTypedViewOwnership(Domain& domain) {
  Child::ptr empty_child;
  Father::ptr const empty_father = std::move(empty_child);
  TEST_ASSERT_FALSE(empty_father.is_valid());
  TEST_ASSERT_FALSE(empty_father.is_loaded());

  Child::ptr unloaded_child =
      Child::ptr::Declare(CreateWith{domain}.with_id(2));
  Father::ptr const unloaded_father = std::move(unloaded_child);
  TEST_ASSERT(unloaded_father.is_valid());
  TEST_ASSERT_FALSE(unloaded_father.is_loaded());
}

void AssertLoadedTypedViewMoveOwnership(Domain& domain) {
  Child::ptr child = Child::ptr::Create(CreateWith{domain}.with_id(1));
  Ptr<Child> const retained_child = child.Load();
  Father::ptr father = std::move(child);
  Ptr<Father> const retained_father = father.Load();

  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  TEST_ASSERT(child.is_valid());
  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  TEST_ASSERT_FALSE(child.is_loaded());
  TEST_ASSERT(retained_father);
  TEST_ASSERT(retained_father.get() == retained_child.get());
  father.Reset();
  TEST_ASSERT(retained_child);
  TEST_ASSERT(retained_father);
}

void AssertLoadedTypedViewMoveAssignment(Domain& domain) {
  auto assigned_child = Child::ptr::Create(CreateWith{domain}.with_id(3));
  Father::ptr assigned_father;
  assigned_father = std::move(assigned_child);
  TEST_ASSERT(assigned_father.is_loaded());
  // NOLINTNEXTLINE(*use-after-move) -- Verify the moved-from contract.
  TEST_ASSERT_FALSE(assigned_child.is_loaded());
}

void TestLoadedTypedViewOwnership(Domain& domain) {
  AssertLoadedTypedViewMoveOwnership(domain);
  AssertLoadedTypedViewMoveAssignment(domain);
}

void TestProxyPtrTypedUpcast(Domain& domain) {
  static_assert(std::is_convertible_v<ProxyPtr<Child>, Ptr<Father>>);
  static_assert(!std::is_convertible_v<ProxyPtr<Father>, Ptr<Child>>);

  auto child = Child::ptr::Create(CreateWith{domain}.with_id(4));
  ProxyPtr<Child> const child_proxy = child.Load();
  Ptr<Father> const father = child_proxy;
  TEST_ASSERT(father);
  TEST_ASSERT(father.get() == child_proxy.get());
}

template <typename O>
void SetFooA(O&& obj_ptr, int value) {
  std::forward<O>(obj_ptr)->a = value;
}

void TestProxyPtr(Domain& domain) {
  static_assert(std::is_same_v<decltype(std::declval<Foo::ptr&>().Load()),
                               ProxyPtr<Foo>>);
  static_assert(std::is_same_v<decltype(std::declval<Foo::ptr const&>().Load()),
                               ProxyPtr<Foo>>);
  static_assert(
      std::is_same_v<decltype(std::declval<Foo::ptr const&>().operator->()),
                     Foo*>);
  static_assert(
      std::is_same_v<decltype(std::declval<Foo::ptr&&>().Load()), Ptr<Foo>>);

  auto foo = CreateCachedFoo(domain);
  auto proxy = foo.Load();
  TEST_ASSERT_EQUAL_PTR(foo.cached().get(), proxy.get());

  SetFooA(foo, 3);
  SetFooA(std::move(foo), 4);
  TEST_ASSERT_EQUAL(4, proxy->a);

  auto generic_result =
      // Verify rvalue access preserves cache.
      // NOLINTNEXTLINE(*use-after-move*)
      foo.WithLoaded([](auto const& loaded) { return loaded->a; });
  TEST_ASSERT(generic_result.has_value());
  TEST_ASSERT_EQUAL(4, *generic_result);

  auto explicit_result =
      foo.WithLoaded([](Ptr<Foo> const& loaded) { return loaded->a; });
  TEST_ASSERT(explicit_result.has_value());
  TEST_ASSERT_EQUAL(4, *explicit_result);
  Foo::ptr const& const_foo = foo;
  TEST_ASSERT(
      const_foo.WithLoaded([](ProxyPtr<Foo> loaded) { loaded->a = 5; }));

  Ptr<Foo> const owned = foo.Load();
  foo.Reset();
  TEST_ASSERT(owned);
  TEST_ASSERT_EQUAL(5, owned->a);

  Foo::ptr empty;
  TEST_ASSERT_FALSE(empty.Load());
  Ptr<Foo> const empty_owned = std::move(empty).Load();
  TEST_ASSERT_FALSE(empty_owned);

  TestProxyPtrTypedUpcast(domain);
}

}  // namespace test_obj_create_internal

void test_createFoo() {
  // create objects
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};
  Domain domain2{ae::Now(), facility};
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

void test_ObjPtrCachedOwnership() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};

  test_obj_create_internal::TestEmptyAndUnloadedPtrOwnership(domain);
  test_obj_create_internal::TestCachedPtrOwnership(domain);
  test_obj_create_internal::TestProxyPtr(domain);
}

void test_ObjPtrTypedViewOwnership() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};

  test_obj_create_internal::TestEmptyAndUnloadedTypedViewOwnership(domain);
  test_obj_create_internal::TestLoadedTypedViewOwnership(domain);
}

void test_createBob() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};

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
  TEST_ASSERT(foo2.Load().get() != foo.Load().get());
  // but internal the same
  TEST_ASSERT(foo2->bar.Load().get() == foo->bar.Load().get());
  // foo is registered and same id loads same object
  Foo::ptr foo3 = Foo::ptr::Declare(CreateWith{domain}.with_id(foo.id()));
  foo3.Load();
  TEST_ASSERT(foo3);
  TEST_ASSERT(foo3.Load().get() == foo.Load().get());
}

void test_cloneFoo() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};
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
  Domain domain{ae::Now(), facility};

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
  Domain domain{ae::Now(), facility};

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
  Domain domain{ae::Now(), facility};
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
  Domain domain{ae::Now(), facility};

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

  TEST_ASSERT(poopa->loopa.Load().get() == loopa.Load().get());
  for (auto& p : loopa->poopas) {
    TEST_ASSERT(poopa.Load().get() == p.Load().get());
  }
}

void test_cyclePoopaLoopaReverse() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};

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
    auto poopa = Poopa::ptr{p};
    TEST_ASSERT(poopa->loopa.Load().get() == loopa.Load().get());
  }
}

void test_Family() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::Now(), facility};
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
  RUN_TEST(ae::test_obj_create::test_ObjPtrCachedOwnership);
  RUN_TEST(ae::test_obj_create::test_ObjPtrTypedViewOwnership);
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
