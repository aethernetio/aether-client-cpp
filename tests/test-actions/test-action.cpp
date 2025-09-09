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

namespace ae::test_action {
class A : public Action<A> {
 public:
  explicit A(ActionContext context) : Action{context} {}

  UpdateStatus Update() const {
    if (result) {
      return UpdateStatus::Result();
    }
    if (error) {
      return UpdateStatus::Error();
    }
    if (stop) {
      return UpdateStatus::Stop();
    }
    return {};
  }

  bool result{};
  bool error{};
  bool stop{};
};

void test_FinishedEvent() {
  auto ap = ActionProcessor{};
  auto a = ActionPtr<A>{ap};

  int finished_count = 0;
  a->FinishedEvent().Subscribe([&]() { finished_count++; });

  // Action not finished yet
  TEST_ASSERT_EQUAL(0, finished_count);

  // Trigger result
  a->result = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, finished_count);

  // Should not fire again
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, finished_count);
}

void test_ActionDestruction() {
  auto ap = ActionProcessor{};
  int event_count = 0;

  {
    auto a = ActionPtr<A>{ap};
    a->StatusEvent().Subscribe(
        [&](auto status) { status.OnResult([&]() { event_count++; }); });

    // Action destroyed before completion
  }

  // Should not trigger any events
  ap.Update(Now());
  TEST_ASSERT_EQUAL(0, event_count);

  // Test after completion
  auto b = ActionPtr<A>{ap};
  b->StatusEvent().Subscribe(
      [&](auto status) { status.OnResult([&]() { event_count++; }); });
  b->result = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, event_count);

  // Should not trigger again
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, event_count);
}

void test_ActionReferenceParameter() {
  auto ap = ActionProcessor{};
  auto a = ActionPtr<A>{ap};

  int result_count = 0;
  A* action_ptr = nullptr;

  // Handler with action reference parameter
  a->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&](A& action) {
      result_count++;
      action_ptr = &action;
    });
  });

  a->result = true;
  ap.Update(Now());

  TEST_ASSERT_EQUAL(1, result_count);
  TEST_ASSERT_NOT_NULL(action_ptr);
  TEST_ASSERT_EQUAL(&*a, action_ptr);
}

}  // namespace ae::test_action

int test_action() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action::test_FinishedEvent);
  RUN_TEST(ae::test_action::test_ActionDestruction);
  RUN_TEST(ae::test_action::test_ActionReferenceParameter);
  return UNITY_END();
}
