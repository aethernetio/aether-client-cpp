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
#include <utility>

#include "aether/tasks/details/task_queues.h"

#include "third_party/etl/include/etl/generic_pool.h"

namespace ae::test_task_queues {
struct RegTask : public ITask {
  static inline std::size_t delete_count = 0;
  explicit RegTask(int id) : task_id{id} {}
  ~RegTask() override { delete_count++; }
  void Invoke() override {}
  int task_id;
};

void test_AddRegularTasks() {
  static constexpr auto kCount = 10;

  auto pool = etl::generic_pool<sizeof(RegTask), alignof(RegTask), kCount>{};
  auto task_queue = TaskQueue<kCount, decltype(pool)>{pool};

  auto* r1 = pool.create<RegTask>(1);
  assert(r1 != nullptr);
  auto res1 = task_queue.Add(r1);
  TEST_ASSERT_TRUE(res1);
  for (int i = 1; i < kCount; i++) {
    auto* ri = pool.create<RegTask>(i);
    auto resi = task_queue.Add(ri);
    TEST_ASSERT_TRUE(resi);
  }
  TEST_ASSERT_EQUAL(10, task_queue.size());
  auto rlast = RegTask{kCount + 1};
  auto reslast = task_queue.Add(&rlast);
  TEST_ASSERT_FALSE(reslast);
}

void test_StealRegularTasks() {
  static constexpr auto kCount = 10;
  RegTask::delete_count = 0;

  auto pool = etl::generic_pool<sizeof(RegTask), alignof(RegTask), kCount>{};
  auto task_queue = TaskQueue<kCount, decltype(pool)>{pool};

  for (int i = 0; i < kCount; i++) {
    auto* ri = pool.create<RegTask>(i);
    auto resi = task_queue.Add(ri);
    TEST_ASSERT_TRUE(resi);
  }

  TEST_ASSERT_EQUAL(10, task_queue.size());
  auto one_task = decltype(task_queue)::list_container<1>{};
  task_queue.StealTasks(one_task);
  TEST_ASSERT_EQUAL(1, one_task.size());
  TEST_ASSERT_EQUAL(0, static_cast<RegTask*>(one_task[0])->task_id);
  TEST_ASSERT_EQUAL(9, task_queue.size());
  // free one_task
  task_queue.Free(one_task);
  TEST_ASSERT_EQUAL(1, RegTask::delete_count);

  auto rest_tasks = decltype(task_queue)::list{};
  task_queue.StealTasks(rest_tasks);
  TEST_ASSERT_EQUAL(9, rest_tasks.size());
  for (int i = 1; i < kCount; ++i) {
    TEST_ASSERT_EQUAL(i, static_cast<RegTask*>(rest_tasks[i - 1])->task_id);
  }
  // free rest_tasks
  task_queue.Free(rest_tasks);
  TEST_ASSERT_EQUAL(kCount, RegTask::delete_count);
}

struct DelayedTask : public IDelayedTask {
  static inline auto delete_count = 0;
  explicit DelayedTask(int id, TimePoint et) : IDelayedTask{et}, task_id{id} {}
  ~DelayedTask() override { delete_count++; }
  void Invoke() override {}
  int task_id;
};

void test_AddDelayedTask() {
  static constexpr auto kCount = 10;

  auto pool =
      etl::generic_pool<sizeof(DelayedTask), alignof(DelayedTask), kCount>{};
  auto delayed_task_queue = DelayedTaskQueue<kCount, decltype(pool)>{pool};

  auto* r0 = pool.create<DelayedTask>(0, TimePoint{});
  auto res = delayed_task_queue.Add(r0);
  TEST_ASSERT_TRUE(res);
  auto epoch = ae::Now();
  // add tasks but in reverse order
  for (int i = 1; i < kCount; ++i) {
    auto* ri = pool.create<DelayedTask>(i, epoch -= std::chrono::seconds{1});
    auto resi = delayed_task_queue.Add(ri);
    TEST_ASSERT_TRUE(resi);
  }
  TEST_ASSERT_EQUAL(kCount, delayed_task_queue.size());
  auto rlast = DelayedTask{kCount + 1, TimePoint{}};
  auto reslast = delayed_task_queue.Add(&rlast);
  TEST_ASSERT_FALSE(reslast);
}

void test_StealDelayedTask() {
  static constexpr auto kCount = 10;
  DelayedTask::delete_count = 0;

  auto pool =
      etl::generic_pool<sizeof(DelayedTask), alignof(DelayedTask), kCount>{};
  auto delayed_task_queue = DelayedTaskQueue<kCount, decltype(pool)>{pool};

  auto epoch = ae::Now();
  // add tasks but in reverse order
  for (int i = 0; i < kCount; ++i) {
    auto* ri = pool.create<DelayedTask>(i, epoch -= std::chrono::seconds{1});
    auto resi = delayed_task_queue.Add(ri);
    TEST_ASSERT_TRUE(resi);
  }
  TEST_ASSERT_EQUAL(kCount, delayed_task_queue.size());

  auto one_task_not_expired = decltype(delayed_task_queue)::list_container<1>{};
  delayed_task_queue.StealTasks(TimePoint::min(), one_task_not_expired);
  TEST_ASSERT_EQUAL(0, one_task_not_expired.size());
  TEST_ASSERT_EQUAL(kCount, delayed_task_queue.size());

  auto one_task_expired = decltype(delayed_task_queue)::list_container<1>{};
  delayed_task_queue.StealTasks(epoch + std::chrono::seconds{1},
                                one_task_expired);
  TEST_ASSERT_EQUAL(1, one_task_expired.size());
  TEST_ASSERT_EQUAL(kCount - 1, delayed_task_queue.size());
  // tasks added in reverse order, the first stealed must be the last added
  TEST_ASSERT_EQUAL(9, static_cast<DelayedTask*>(one_task_expired[0])->task_id);
  delayed_task_queue.Free(one_task_expired);
  TEST_ASSERT_EQUAL(1, DelayedTask::delete_count);

  auto expired_half = decltype(delayed_task_queue)::list{};
  delayed_task_queue.StealTasks(epoch + std::chrono::seconds{4}, expired_half);
  TEST_ASSERT_EQUAL(4, expired_half.size());
  // check the order
  for (std::size_t i = 0; i < expired_half.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(i + 5),
                      static_cast<DelayedTask*>(expired_half[i])->task_id);
  }
  delayed_task_queue.Free(expired_half);
  TEST_ASSERT_EQUAL(5, DelayedTask::delete_count);

  auto expired_all = decltype(delayed_task_queue)::list{};
  delayed_task_queue.StealTasks(epoch + std::chrono::seconds{kCount},
                                expired_all);
  TEST_ASSERT_EQUAL(5, expired_all.size());
  // check the order
  for (std::size_t i = 0; i < expired_all.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(i),
                      static_cast<DelayedTask*>(expired_all[i])->task_id);
  }
  delayed_task_queue.Free(expired_all);
  TEST_ASSERT_EQUAL(10, DelayedTask::delete_count);
}

}  // namespace ae::test_task_queues

int test_task_queues() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_task_queues::test_AddRegularTasks);
  RUN_TEST(ae::test_task_queues::test_StealRegularTasks);
  RUN_TEST(ae::test_task_queues::test_AddDelayedTask);
  RUN_TEST(ae::test_task_queues::test_StealDelayedTask);
  return UNITY_END();
}
