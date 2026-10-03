/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/events/details/event_key_registry.h"

namespace ae::test_event_key_registry {
using events::Key;
using events::KeyRegistry;

void test_RegisterEvents() {
  auto reg = KeyRegistry<5>{};
  // check if registry register a new key
  auto k0 = reg.Register();
  TEST_ASSERT_TRUE(k0.has_value());
  TEST_ASSERT_EQUAL(0, k0.value());

  // check if registry register another key and they are contiguous
  auto k1 = reg.Register();
  TEST_ASSERT_TRUE(k1.has_value());
  TEST_ASSERT_EQUAL(1, k1.value());
  auto k2 = reg.Register();
  TEST_ASSERT_TRUE(k2.has_value());
  TEST_ASSERT_EQUAL(2, k2.value());

  // check if key registered
  TEST_ASSERT_TRUE(reg.IsRegistered(k0.value()));
  TEST_ASSERT_TRUE(reg.IsRegistered(k1.value()));
  TEST_ASSERT_TRUE(reg.IsRegistered(k2.value()));

  // unregister key
  reg.Retire(k1.value());

  // check if all except unregistered keys still registered
  TEST_ASSERT_TRUE(reg.IsRegistered(k0.value()));
  TEST_ASSERT_FALSE(reg.IsRegistered(k1.value()));
  TEST_ASSERT_TRUE(reg.IsRegistered(k2.value()));

  // unregister another one and repeat
  reg.Retire(k0.value());
  reg.Release(k0.value());

  TEST_ASSERT_FALSE(reg.IsRegistered(k0.value()));
  TEST_ASSERT_FALSE(reg.IsRegistered(k1.value()));
  TEST_ASSERT_TRUE(reg.IsRetiring(k1.value()));
  TEST_ASSERT_TRUE(reg.IsRegistered(k2.value()));
}

void test_RegisterOverflow() {
  auto reg = KeyRegistry<5>{};
  // register all 5
  std::vector<Key> keys;
  for (auto i = 0; i < 5; i++) {
    auto k = reg.Register();
    TEST_ASSERT_TRUE(k.has_value());
    keys.emplace_back(k.value());
  }
  TEST_ASSERT_EQUAL(5, keys.size());

  // new register should fail
  auto k_extra = reg.Register();
  TEST_ASSERT_FALSE(k_extra.has_value());

  // after unregister one
  // new registration should be success
  reg.Retire(keys.at(2));
  reg.Release(keys.at(2));
  auto k_new = reg.Register();
  TEST_ASSERT_TRUE(k_new.has_value());
  // and it's still contiguous
  TEST_ASSERT_EQUAL(keys[4] + 1, k_new.value());
}

void test_IsRegistered() {
  auto reg = KeyRegistry<5>{};

  auto k = reg.Register();
  TEST_ASSERT_TRUE(reg.IsRegistered(k.value()));

  // check if any random values are not registered
  TEST_ASSERT_FALSE(reg.IsRegistered(1));
  TEST_ASSERT_FALSE(reg.IsRegistered(6));
  TEST_ASSERT_FALSE(reg.IsRegistered(4));
}

}  // namespace ae::test_event_key_registry

int test_event_key_registry() {
  using namespace ae::test_event_key_registry;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_RegisterEvents);
  RUN_TEST(test_RegisterOverflow);
  RUN_TEST(test_IsRegistered);
  return UNITY_END();
}
