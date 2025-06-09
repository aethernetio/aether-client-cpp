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

#include <optional>

#include "aether/actions/action.h"
#include "aether/actions/action_view.h"
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

  ActionResult Update() { return ActionResult::Result(); }
};

void test_CreateSomeActions() {
  auto context = TestActionContext{};

  auto a = std::optional<A>{};
  a.emplace(context);

  auto a_view = ActionView{*a};
  TEST_ASSERT(static_cast<bool>(a_view));

  a.reset();
  TEST_ASSERT(!static_cast<bool>(a_view));

  auto a1 = std::optional<A>{};
  a1.emplace(context);

  bool finished = false;
  auto s1 = a1->FinishedEvent().Subscribe([&] { finished = true; });
  a1.reset();
  TEST_ASSERT(!finished);

  auto a2 = std::optional<A>{};
  a2.emplace(context);

  auto s2 = a2->FinishedEvent().Subscribe([&] {
    finished = true;
    a2.reset();
  });

  // constexpr bool ah = action_internal::HasUpdate<A>::value;

  a2->ActionUpdate(Now());
  TEST_ASSERT(finished);

  std::size_t count = 0;
  for (auto const& aref : context.action_registry) {
    TEST_ASSERT(aref.action == nullptr);
    ++count;
  }
  // a_view still holds 1 entry
  TEST_ASSERT_EQUAL(1, count);
}

void test_IterateOverActions() {
  auto context = TestActionContext{};

  auto a0 = std::optional<A>{};
  a0.emplace(context);

  auto a1 = std::optional<A>{};
  a1.emplace(context);

  auto a2 = std::optional<A>{};
  a2.emplace(context);
  {
    std::size_t count = 0;
    for (auto& aref : context.action_registry) {
      TEST_ASSERT_NOT_EQUAL(nullptr, aref.action);
      TEST_ASSERT_EQUAL(1, aref.counter);
      ++count;
    }

    TEST_ASSERT_EQUAL(3, count);
  }

  // reset some actions
  a0.reset();
  {
    std::size_t count = 0;
    for (auto const& aref : context.action_registry) {
      TEST_ASSERT_NOT_EQUAL(nullptr, aref.action);
      TEST_ASSERT_EQUAL(1, aref.counter);
      ++count;
    }
    TEST_ASSERT_EQUAL(2, count);
  }

  // add view to action
  auto a2_view = ActionView{*a2};
  {
    std::size_t count = 0;
    for (auto const& aref : context.action_registry) {
      TEST_ASSERT_NOT_EQUAL(nullptr, aref.action);

      if (count == 1) {
        TEST_ASSERT_EQUAL(2, aref.counter);
      } else {
        TEST_ASSERT_EQUAL(1, aref.counter);
      }
      ++count;
    }
    TEST_ASSERT_EQUAL(2, count);
  }
  // remove more actions
  a2.reset();
  {
    std::size_t count = 0;
    for (auto const& aref : context.action_registry) {
      if (count == 1) {
        // a2_view hold a reference but pointer is null
        TEST_ASSERT_EQUAL(nullptr, aref.action);
      } else {
        TEST_ASSERT_NOT_EQUAL(nullptr, aref.action);
      }
      TEST_ASSERT_EQUAL(1, aref.counter);
      ++count;
    }
    TEST_ASSERT_EQUAL(2, count);
  }
  // remove all
  a1.reset();
  {
    std::size_t count = 0;
    for (auto const& aref : context.action_registry) {
      // a2_view hold a reference but pointer is null
      TEST_ASSERT_EQUAL(nullptr, aref.action);
      TEST_ASSERT_EQUAL(1, aref.counter);
      ++count;
    }
    TEST_ASSERT_EQUAL(1, count);
  }
}

void test_ActionMigration() {
  auto context = TestActionContext{};
  {
    auto a1 = A{context};
    auto a_view = ActionView{a1};
    TEST_ASSERT(static_cast<bool>(a_view));

    auto a2 = std::move(a1);
    TEST_ASSERT(static_cast<bool>(a_view));
    TEST_ASSERT_EQUAL(&a2, &*a_view);
  }
  {
    std::vector<A> actions;
    std::vector<ActionView<A>> action_views;
    for (auto i = 0; i < 10; ++i) {
      auto& ref = actions.emplace_back(context);
      action_views.emplace_back(ref);
    }

    for (std::size_t i = 0; i < 10; ++i) {
      TEST_ASSERT_EQUAL(&actions[i], &*action_views[i]);
    }
    for (auto& entry : context.action_registry) {
      TEST_ASSERT_EQUAL(2, entry.counter);
      TEST_ASSERT_NOT_EQUAL(nullptr, entry.action);
    }
    actions.clear();
    for (auto& entry : context.action_registry) {
      TEST_ASSERT_EQUAL(1, entry.counter);
      TEST_ASSERT_EQUAL(nullptr, entry.action);
    }
  }
  TEST_ASSERT_EQUAL(0, context.action_registry.size());
}

}  // namespace ae::test_action_registry

int test_action_registry() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_registry::test_CreateSomeActions);
  RUN_TEST(ae::test_action_registry::test_IterateOverActions);
  RUN_TEST(ae::test_action_registry::test_ActionMigration);
  return UNITY_END();
}
