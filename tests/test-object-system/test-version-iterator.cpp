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

#include "aether/obj/version_iterator.h"

namespace ae {
template <auto I, typename _ = void>
struct VersionAllowed : std::false_type {};
template <auto I>
struct VersionAllowed<I, std::void_t<decltype(Version<I>{})>> : std::true_type {
};

void test_MaxVersion() {
  Version<0> v;                      // ok!
  Version<MAX_VERSION> max_version;  // ok!
  // Version<MAX_VERSION + 1> to_big_version;  // not ok!

  static_assert(VersionAllowed<0>::value, "Version<0> must be allowed");
  static_assert(VersionAllowed<MAX_VERSION>::value,
                "Version<MAX_VERSION> must be allowed");
  static_assert(!VersionAllowed<MAX_VERSION + 1>::value,
                "Version<MAX_VERSION + 1> must not be allowed");
}

struct TestObject {
  // loads
  template <typename Dnv>
  void Load(Version<0> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Load(Version<1> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Load(Version<2> v, Dnv& dnv) {
    dnv(v);
  }

  // saves
  template <typename Dnv>
  void Save(Version<0> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Save(Version<1> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Save(Version<2> v, Dnv& dnv) {
    dnv(v);
  }
};

struct TestObject2 {
  // loads
  template <typename Dnv>
  void Load(Version<MAX_VERSION - 3> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Load(Version<MAX_VERSION - 2> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Load(Version<MAX_VERSION - 1> v, Dnv& dnv) {
    dnv(v);
  }

  // saves
  template <typename Dnv>
  void Save(Version<MAX_VERSION - 2> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Save(Version<MAX_VERSION - 1> v, Dnv& dnv) {
    dnv(v);
  }

  template <typename Dnv>
  void Save(Version<MAX_VERSION> v, Dnv& dnv) {
    dnv(v);
  }
};

void test_HasVersionedTraits() {
  static_assert(HasAnyVersionedLoad<TestObject>::value, "Load(Version)");
  static_assert(HasVersionedLoad<TestObject, 0>::value, "Load(Version<0>)");
  static_assert(HasVersionedLoad<TestObject, 1>::value, "Load(Version<1>)");
  static_assert(HasVersionedLoad<TestObject, 2>::value, "Load(Version<2>)");
  static_assert(!HasVersionedLoad<TestObject, 3>::value, "!Load(Version<3>)");

  static_assert(HasAnyVersionedSave<TestObject>::value, "Save(Version)");
  static_assert(HasVersionedSave<TestObject, 0>::value, "Save(Version<0>)");
  static_assert(HasVersionedSave<TestObject, 1>::value, "Save(Version<1>)");
  static_assert(HasVersionedSave<TestObject, 2>::value, "Save(Version<2>)");
  static_assert(!HasVersionedSave<TestObject, 3>::value, "!Save(Version<3>)");
}

void test_ObjectVersionBounds() {
  constexpr auto object1_load_bounds = VersionedLoadMinMax<TestObject>::value;
  static_assert(0 == object1_load_bounds.first);
  static_assert(2 == object1_load_bounds.second);

  constexpr auto object1_save_bounds = VersionedSaveMinMax<TestObject>::value;
  static_assert(0 == object1_save_bounds.first);
  static_assert(2 == object1_save_bounds.second);

  constexpr auto object2_load_bounds = VersionedLoadMinMax<TestObject2>::value;
  static_assert(MAX_VERSION - 3 == object2_load_bounds.first);
  static_assert(MAX_VERSION - 1 == object2_load_bounds.second);

  constexpr auto object2_save_bounds = VersionedSaveMinMax<TestObject2>::value;
  static_assert(MAX_VERSION - 2 == object2_save_bounds.first);
  static_assert(MAX_VERSION == object2_save_bounds.second);
}

template <typename TFactory>
void VersionIteratorLoadTestFunc(TFactory factory) {
  auto [obj, expected_count] = factory();

  using ObjectType = std::decay_t<decltype(obj)>;

  int visit_count = 0;
  constexpr auto version_bounds = VersionedLoadMinMax<ObjectType>::value;
  IterateVersions<HasVersionedLoad, version_bounds.first,
                  version_bounds.second>(
      obj, [&visit_count](auto version, auto& obj) { ++visit_count; });

  TEST_ASSERT_EQUAL(expected_count, visit_count);
}

template <typename TFactory>
void VersionIteratorSaveTestFunc(TFactory factory) {
  auto [obj, expected_count] = factory();

  using ObjectType = std::decay_t<decltype(obj)>;

  int visit_count = 0;
  constexpr auto version_bounds = VersionedSaveMinMax<ObjectType>::value;
  IterateVersions<HasVersionedSave, version_bounds.first,
                  version_bounds.second>(
      obj, [&visit_count](auto version, auto& obj) { ++visit_count; });

  TEST_ASSERT_EQUAL(expected_count, visit_count);
}

void test_VersionIterator() {
  auto factory1 = []() { return std::make_pair(TestObject{}, 3); };
  auto factory2 = []() { return std::make_pair(TestObject2{}, 3); };

  VersionIteratorLoadTestFunc(factory1);
  VersionIteratorLoadTestFunc(factory2);

  VersionIteratorSaveTestFunc(factory1);
  VersionIteratorSaveTestFunc(factory2);
}
}  // namespace ae

int run_test_version_iterator() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_MaxVersion);
  RUN_TEST(ae::test_HasVersionedTraits);
  RUN_TEST(ae::test_ObjectVersionBounds);
  RUN_TEST(ae::test_VersionIterator);
  return UNITY_END();
}
