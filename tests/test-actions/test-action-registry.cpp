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

#include "aether/actions/action.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_registry.h"
#include "aether/actions/action_trigger.h"

namespace ae::test_action_registry {
struct TestActionContext {
  ActionRegistry& get_registry() { return action_registry; }
  ActionTrigger& get_trigger() { return action_trigger; }

  ActionRegistry action_registry;
  ActionTrigger action_trigger;
};

struct A : public Action<A> {
  using Action::Action;
  using Action::operator=;

  UpdateStatus Update() { return UpdateStatus::Result(); }
};

void test_ActionPtrCreate() {
  auto context = TestActionContext{};
  TEST_ASSERT_EQUAL(0, context.action_registry.size());

  auto a = ActionPtr<A>{context};
  TEST_ASSERT_EQUAL(1, context.action_registry.size());

  context.action_registry.Remove(context.action_registry.begin());
  TEST_ASSERT_EQUAL(0, context.action_registry.size());
}

void test_ActionRegistryIteration() {
  auto context = TestActionContext{};
  for (std::size_t i = 0; i < 10; ++i) {
    auto add = ActionPtr<A>{context};
  }

  TEST_ASSERT_EQUAL(10, context.action_registry.size());

  std::size_t index = 0;
  for (auto it = context.action_registry.begin();
       it != context.action_registry.end();) {
    if (index % 2 == 0) {
      auto* pre_remove = it->get();
      it = context.action_registry.Remove(it);
      if (it != context.action_registry.end()) {
        TEST_ASSERT_NOT_EQUAL(pre_remove, it->get());
      }
    } else {
      ++it;
    }
    index++;
  }
  TEST_ASSERT_EQUAL(5, context.action_registry.size());
}

}  // namespace ae::test_action_registry

int test_action_registry() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_registry::test_ActionPtrCreate);
  RUN_TEST(ae::test_action_registry::test_ActionRegistryIteration);
  return UNITY_END();
}
