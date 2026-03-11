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
#include <thread>

#include "aether/tasks/details/manual_task_scheduler.h"

namespace ae::test_manual_task_scheduler {
void test_ManualScheduler() {
  static constexpr auto kCount = 10;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};
  std::array<bool, kCount> invoked{};

  for (auto i = 0; i < kCount; ++i) {
    task_sched.Task([&, i]() { invoked[i] = true; });
  }
  auto tp = task_sched.Update();
  // returned tp must be a max time
  TEST_ASSERT_EQUAL(TimePoint::max().time_since_epoch().count(),
                    tp.time_since_epoch().count());

  // check if all invoked
  for (auto i : invoked) {
    TEST_ASSERT_TRUE(i);
  }

  // add delayed tasks
  for (auto i = 0; i < kCount; ++i) {
    task_sched.DelayedTask([&, i]() { invoked[i] = false; }, 1s);
  }

  auto epoch = Now();
  auto epoch_end = epoch + 10s;
  std::size_t count = 0;
  while (epoch < epoch_end) {
    count++;
    auto tp0 = task_sched.Update(epoch += 1s);
    if (tp != TimePoint::max()) {
      // check if tp is later than current epoch
      TEST_ASSERT_GREATER_OR_EQUAL(tp.time_since_epoch().count(),
                                   epoch.time_since_epoch().count());
    }
  }
  TEST_ASSERT_EQUAL(kCount, count);
  // test all invoked
  for (auto i : invoked) {
    TEST_ASSERT_FALSE(i);
  }
}

void test_Multithread() {
  static constexpr auto kCount = 1000;
  static constexpr auto kThreadCount = 4;
  static constexpr auto kTarget = kCount;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};

  std::array<std::thread, kThreadCount> workers;
  std::array<int, kThreadCount> work_counters{};
  for (int i = 0; i < kThreadCount; ++i) {
    workers[i] = std::thread{[&, i]() {
      while (work_counters[i] < kTarget) {
        task_sched.Task([&, i]() { work_counters[i]++; });
        std::this_thread::sleep_for(100us);
      }
    }};
  }

  auto updater = std::thread{[&]() {
    bool do_work = true;
    while (do_work) {
      // printf("update\n");
      task_sched.Update();

      // check if all finished
      do_work = false;
      for (auto wc : work_counters) {
        if (wc < kTarget) {
          do_work = true;
          break;
        }
      }
      std::this_thread::sleep_for(100us);
    }
  }};

  updater.join();
  for (auto& w : workers) {
    if (w.joinable()) {
      w.join();
    }
  }
}

void test_DelayedTiming() {
  static constexpr auto kCount = 10;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};

  auto before = Now();
  bool invoked = false;
  task_sched.DelayedTask(
      [&]() {
        // check if invoked after 100ms
        TEST_ASSERT_GREATER_OR_EQUAL(
            (100ms).count(),
            std::chrono::duration_cast<std::chrono::milliseconds>(Now() -
                                                                  before)
                .count());
        invoked = true;
      },
      100ms);
  auto tp = task_sched.Update();
  // check if wait time is 100ms
  TEST_ASSERT_GREATER_OR_EQUAL(
      (100ms).count(),
      std::chrono::duration_cast<std::chrono::milliseconds>(tp - before)
          .count());
  task_sched.WaitUntil(tp);
  task_sched.Update();
  TEST_ASSERT_TRUE(invoked);
}

}  // namespace ae::test_manual_task_scheduler

int test_manual_task_scheduler() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_manual_task_scheduler::test_ManualScheduler);
  RUN_TEST(ae::test_manual_task_scheduler::test_Multithread);
  RUN_TEST(ae::test_manual_task_scheduler::test_DelayedTiming);
  return UNITY_END();
}
