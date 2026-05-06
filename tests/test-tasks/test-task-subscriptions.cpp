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

#include "unity.h"

#include "aether/tasks/details/task.h"
#include "aether/tasks/details/generic_task.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/tasks/details/manual_task_scheduler.h"

namespace ae::test_task_subscriptions {
void test_CreateSubscription() {
  // creartable without arguments
  auto sub0 = TaskSubscription{};
  // and it's invalid
  TEST_ASSERT_FALSE(sub0);

  auto task1 = GenericTask{[&]() {}};
  // implicitly created from pointer to task
  TaskSubscription sub1 = &task1;
  // this valid``
  TEST_ASSERT_TRUE(sub1);

  // created from move construction
  auto sub1_1 = std::move(sub1);
  // now this valid
  TEST_ASSERT_TRUE(sub1_1);
  // this not
  TEST_ASSERT_FALSE(sub1);

  // initiated from assignment
  TaskSubscription sub1_2;
  TEST_ASSERT_FALSE(sub1_2);
  sub1_2 = std::move(sub1_1);
  // becomes valid
  TEST_ASSERT_TRUE(sub1_2);
  // and now this not
  TEST_ASSERT_FALSE(sub1_1);
}

void test_TrackActiveState() {
  auto task0 = GenericTask{[&]() {}};
  // newly created task active
  TEST_ASSERT_TRUE(task0.active != 0);

  auto sub0 = TaskSubscription{&task0};
  TEST_ASSERT_TRUE(sub0);
  sub0.Reset();
  // task is not active
  TEST_ASSERT_FALSE(task0.active != 0);

  auto task1 = GenericTask{[]() {}};
  TEST_ASSERT_TRUE(task1.active != 0);
  // check RAII
  {
    auto sub = TaskSubscription{&task1};
    // on sub destruction task is deactevated
  }
  TEST_ASSERT_FALSE(task1.active != 0);
}

void test_SubscriptionInScheduler() {
  auto scheduler = ManualTaskScheduler<TaskManagerConf<10>>{};

  bool executed = false;

  auto not_sub1 = scheduler.Task([&]() { executed = true; });  // NOLINT
  // this is not a subscription actually
  static_assert(!std::is_same_v<TaskSubscription, decltype(not_sub1)>);
  // this is subscription
  TaskSubscription sub1 = not_sub1;
  TEST_ASSERT_TRUE(sub1);
  // run tasks
  scheduler.Update();
  // must be executed
  TEST_ASSERT_TRUE(executed);
  // since the task was removed subscription is invalid now
  TEST_ASSERT_FALSE(sub1);

  executed = false;
  TaskSubscription sub2 = scheduler.Task([&]() { executed = true; });

  sub2.Reset();
  scheduler.Update();

  // since the subscription was reset task must not be executed
  TEST_ASSERT_FALSE(executed);
  TEST_ASSERT_FALSE(sub2);
}

}  // namespace ae::test_task_subscriptions

int test_task_subscriptions() {
  using namespace ae::test_task_subscriptions;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_CreateSubscription);
  RUN_TEST(test_TrackActiveState);
  RUN_TEST(test_SubscriptionInScheduler);
  return UNITY_END();
}
