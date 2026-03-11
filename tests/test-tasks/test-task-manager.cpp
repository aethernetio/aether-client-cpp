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
#include <array>

#include "aether/tasks/details/task_manager.h"

namespace ae::test_task_manager {

void test_TaskManagerAddRegular() {
  static constexpr auto kCount = 10;

  auto task_manager = TaskManager<TaskManagerConf<kCount>>{};

  std::array<bool, kCount> invoked{};
  for (auto i = 0; i < kCount; i++) {
    auto res = task_manager.Task([&, i]() { invoked[i] = true; });
    TEST_ASSERT_TRUE(res);
  }
  auto res = task_manager.Task([&, i{0}]() { invoked[i] = true; });
  TEST_ASSERT_FALSE(res);

  auto tasks = std::decay_t<decltype(task_manager.regular())>::list{};
  task_manager.regular().StealTasks(tasks);
  TEST_ASSERT_EQUAL(kCount, tasks.size());
  for (auto* t : tasks) {
    t->Invoke();
  }
  for (auto i : invoked) {
    TEST_ASSERT_TRUE(i);
  }
  task_manager.regular().Free(tasks);
}

void test_TaskManagerAddDelayed() {
  static constexpr auto kCount = 10;

  auto task_manager = TaskManager<TaskManagerConf<kCount>>{};

  std::array<bool, kCount> invoked{};
  auto epoch = Now();
  for (auto i = 0; i < kCount; i++) {
    auto res = task_manager.DelayedTask([&, i]() { invoked[i] = true; },
                                        epoch + i * 1s);
    TEST_ASSERT_TRUE(res);
  }

  // steal only half of tasks
  auto tasks = std::decay_t<decltype(task_manager.delayed())>::list{};
  task_manager.delayed().StealTasks(epoch + 4s, tasks);
  TEST_ASSERT_EQUAL(5, tasks.size());
  for (auto* t : tasks) {
    t->Invoke();
  }
  for (std::size_t i = 0; i < tasks.size(); i++) {
    TEST_ASSERT_TRUE(invoked[i]);
  }
  task_manager.delayed().Free(tasks);

  // add one task by duration
  task_manager.DelayedTask([&]() { invoked[0] = false; }, 1s);

  // get all expired tasks
  auto tasks_rest = std::decay_t<decltype(task_manager.delayed())>::list{};
  task_manager.delayed().StealTasks(epoch + 10s, tasks_rest);
  TEST_ASSERT_EQUAL(6, tasks_rest.size());
  for (auto* t : tasks_rest) {
    t->Invoke();
  }
  TEST_ASSERT_FALSE(invoked[0]);

  task_manager.delayed().Free(tasks_rest);
}

}  // namespace ae::test_task_manager

int test_task_manager() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_task_manager::test_TaskManagerAddRegular);
  RUN_TEST(ae::test_task_manager::test_TaskManagerAddDelayed);
  return UNITY_END();
}
