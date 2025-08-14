/*
 * Copyright 2025 Aethernet Inc.
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
#include "aether/actions/action_processor.h"

namespace ae::test_action_processor {
class A : public Action<A> {
 public:
  using Action::Action;

  UpdateStatus Update() {
    if (res) {
      return UpdateStatus::Result();
    }
    return {};
  }

  bool res = false;
};

class SpawnA : public Action<SpawnA> {
 public:
  explicit SpawnA(ActionContext action_context)
      : Action{action_context}, action_context_{action_context} {}

  UpdateStatus Update() {
    if (!a) {
      a = ActionPtr<A>{action_context_};
    }
    if (a->res) {
      return UpdateStatus::Result();
    }
    return {};
  }

  ActionContext action_context_;
  ActionPtr<A> a;
};

void test_ActionProcessA() {
  auto ap = ActionProcessor{};

  auto a1 = ActionPtr<A>{ap};
  auto a2 = ActionPtr<A>{ap};
  auto a3 = ActionPtr<A>{ap};

  TEST_ASSERT_EQUAL(3, ap.get_registry().size());

  ap.Update(Now());

  TEST_ASSERT_EQUAL(3, ap.get_registry().size());
  // result 1 - one action is removed
  a1->res = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(2, ap.get_registry().size());

  // do nothing and nothing is changed
  ap.Update(Now());
  TEST_ASSERT_EQUAL(2, ap.get_registry().size());

  auto a4 = ActionPtr<A>{ap};
  ap.Update(Now());
  TEST_ASSERT_EQUAL(3, ap.get_registry().size());

  a2->res = true;
  a3->res = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, ap.get_registry().size());
}

void test_SpawnActionDuringUpdate() {
  auto ap = ActionProcessor{};

  auto a1 = ActionPtr<A>{ap};
  auto sa = ActionPtr<SpawnA>{ap};
  TEST_ASSERT_EQUAL(2, ap.get_registry().size());

  // new action should be spawned in sa
  ap.Update(Now());
  TEST_ASSERT_EQUAL(3, ap.get_registry().size());

  a1->res = true;
  sa->a->res = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(0, ap.get_registry().size());
}

}  // namespace ae::test_action_processor

int test_action_processor() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_processor::test_ActionProcessA);
  RUN_TEST(ae::test_action_processor::test_SpawnActionDuringUpdate);

  return UNITY_END();
}
