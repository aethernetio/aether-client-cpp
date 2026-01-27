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
#include "map_domain_storage.h"
#include "objects/seven_fridays.h"

namespace ae::test_update_objects {
// replace class id in map facility to imitate one class save and load
void replace_class_id(MapDomainStorage& facility, std::uint32_t old_class,
                      std::uint32_t new_class) {
  for (auto& [_, classes] : facility.map) {
    auto it = classes.find(old_class);
    if (it != classes.end()) {
      classes[new_class] = std::move(it->second);
      classes.erase(it);
    }
  }
}

// remove class id from map facility to imitate one class was removed from
// inheritance chain
void remove_class_id(MapDomainStorage& facility, std::uint32_t class_id) {
  for (auto& [_, classes] : facility.map) {
    classes.erase(class_id);
  }
}

void test_increaseVersion() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  // the oldest version
  {
    Friday0::ptr friday = Friday0::ptr::Create(CreateWith{domain}.with_id(1));
    friday.Save();
  }

  // load version 1 from version 0
  replace_class_id(facility, Friday0::kClassId, Friday1::kClassId);
  {
    Friday1::ptr friday = Friday1::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL(22, p->a);
      TEST_ASSERT_EQUAL(23, p->b);
      p->a = 123;
      p->b = 431;
    });
    friday.Save();
  }

  // load version 2 from version 1
  replace_class_id(facility, Friday1::kClassId, Friday2::kClassId);
  {
    Friday2::ptr friday = Friday2::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL_FLOAT(123.f, p->a);
      p->a = 42.42f;
    });
    friday.Save();
  }

  // load version 3 from version 2
  replace_class_id(facility, Friday2::kClassId, Friday3::kClassId);
  {
    Friday3::ptr friday = Friday3::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& p) {
      TEST_ASSERT(p);
      TEST_ASSERT_EQUAL(42.42f, p->a);
      TEST_ASSERT_EQUAL_STRING("", p->x.c_str());
    });
  }
}

void test_decreaseVersion() {
  auto facility = MapDomainStorage{};
  Domain domain{ae::ClockType::now(), facility};
  // the newest version
  {
    Friday3::ptr friday = Friday3::ptr::Create(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      f->x = "hello";
      f->a = 123.123f;
    });
    friday.Save();
  }

  // load version 2 from version 3
  replace_class_id(facility, Friday3::kClassId, Friday2::kClassId);
  // it's not allowed to remove class from inheritance chain and leave it in
  // registry
  remove_class_id(facility, Hoopa::kClassId);
  {
    Friday2::ptr friday = Friday2::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      TEST_ASSERT(f);
      TEST_ASSERT_EQUAL(123.123f, f->a);
    });
    friday.Save();
  }

  // load version 1 from version 2
  replace_class_id(facility, Friday2::kClassId, Friday1::kClassId);
  {
    Friday1::ptr friday = Friday1::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) {
      TEST_ASSERT(f);
      TEST_ASSERT_EQUAL(123, f->a);
      TEST_ASSERT_EQUAL(0, f->b);
      f->b = 23;
    });
    friday.Save();
  }

  // load version 0 from version 1
  replace_class_id(facility, Friday1::kClassId, Friday0::kClassId);
  {
    Friday0::ptr friday = Friday0::ptr::Declare(CreateWith{domain}.with_id(1));
    friday.WithLoaded([](auto const& f) { TEST_ASSERT(f); });
  }
}
}  // namespace ae::test_update_objects

int run_test_update_objects() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_update_objects::test_increaseVersion);
  RUN_TEST(ae::test_update_objects::test_decreaseVersion);
  return UNITY_END();
}
