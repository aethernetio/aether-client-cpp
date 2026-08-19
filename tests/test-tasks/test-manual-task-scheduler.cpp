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
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "aether/tasks/details/manual_task_scheduler.h"
#include "aether/tasks/details/task_subsctiption.h"

namespace ae::test_manual_task_scheduler {
using namespace std::chrono_literals;
using TimePoint = std::chrono::system_clock::time_point;
static auto Now() { return TimePoint::clock::now(); }

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

void test_ReclaimInactiveOnTaskWithoutUpdate() {
  static constexpr auto kCount = 8;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};

  std::array<TaskSubscription, kCount> subs{};
  for (auto i = 0; i < kCount; ++i) {
    subs[i] = task_sched.DelayedTask([]() {}, 60s);
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(subs[i]),
                             "expected delayed task allocation");
  }
  TEST_ASSERT_TRUE_MESSAGE(task_sched.Task([]() {}) == nullptr,
                           "pool should be full before reclaim");

  for (auto& sub : subs) {
    sub.Reset();
  }

  // Next Task() must reclaim cancelled delayed tasks without calling Update().
  auto again = task_sched.Task([]() {});
  TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(again),
                           "Task() must reclaim cancelled delayed slots");
  (void)task_sched.Update(Now());
}

void test_ActiveDelayedTasksBlockAllocation() {
  static constexpr auto kCount = 8;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};

  std::array<TaskSubscription, kCount> subs{};
  for (auto i = 0; i < kCount; ++i) {
    subs[i] = task_sched.DelayedTask([]() {}, 60s);
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(subs[i]),
                             "expected delayed task allocation");
  }

  TEST_ASSERT_TRUE_MESSAGE(task_sched.Task([]() {}) == nullptr,
                           "active delayed tasks must keep pool full");
  TEST_ASSERT_TRUE_MESSAGE(task_sched.DelayedTask([]() {}, 60s) == nullptr,
                           "active delayed tasks must keep pool full");

  for (auto& sub : subs) {
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(sub),
                             "existing delayed subscriptions must stay valid");
  }
}

void test_ResetUpdateRestoresAllSlots() {
  static constexpr auto kCount = 8;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kCount>>{};

  std::array<TaskSubscription, kCount> subs{};
  for (auto i = 0; i < kCount; ++i) {
    subs[i] = task_sched.DelayedTask([]() {}, 60s);
    TEST_ASSERT_TRUE(static_cast<bool>(subs[i]));
  }
  for (auto& sub : subs) {
    sub.Reset();
  }
  (void)task_sched.Update(Now());

  std::array<TaskSubscription, kCount> again{};
  for (auto i = 0; i < kCount; ++i) {
    again[i] = task_sched.Task([]() {});
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(again[i]),
                             "all slots must be available after Reset+Update");
  }
  (void)task_sched.Update(Now());
}

void test_MultithreadCancelledDelayedVsRegular() {
  static constexpr auto kPool = 32;
  static constexpr auto kIterations = 200;
  auto task_sched = ManualTaskScheduler<TaskManagerConf<kPool>>{};

  std::atomic_bool start{false};
  std::atomic_bool stop{false};
  std::atomic_bool consumer_done{false};
  std::atomic_int accepted_regular{0};
  std::atomic_int executed_regular{0};
  std::mutex regular_subs_mu;
  std::vector<TaskSubscription> regular_subs;
  regular_subs.reserve(kIterations + kPool);

  auto producer = std::thread{[&]() {
    while (!start.load(std::memory_order::acquire)) {
    }
    for (int i = 0; i < kIterations; ++i) {
      TaskSubscription sub = task_sched.DelayedTask([]() {}, 60s);
      if (sub) {
        sub.Reset();
      }
    }
    stop.store(true, std::memory_order::release);
  }};

  auto consumer = std::thread{[&]() {
    while (!start.load(std::memory_order::acquire)) {
    }
    while (!stop.load(std::memory_order::acquire)) {
      TaskSubscription sub =
          task_sched.Task([&]() { executed_regular.fetch_add(1); });
      if (sub) {
        accepted_regular.fetch_add(1);
        std::scoped_lock lock{regular_subs_mu};
        regular_subs.push_back(std::move(sub));
      }
    }
    for (int i = 0; i < kPool; ++i) {
      TaskSubscription sub =
          task_sched.Task([&]() { executed_regular.fetch_add(1); });
      if (sub) {
        accepted_regular.fetch_add(1);
        std::scoped_lock lock{regular_subs_mu};
        regular_subs.push_back(std::move(sub));
      }
    }
    consumer_done.store(true, std::memory_order::release);
  }};

  auto updater = std::thread{[&]() {
    while (!start.load(std::memory_order::acquire)) {
    }
    while (!consumer_done.load(std::memory_order::acquire) ||
           executed_regular.load() < accepted_regular.load()) {
      (void)task_sched.Update(Now());
    }
    (void)task_sched.Update(Now());
  }};

  start.store(true, std::memory_order::release);
  producer.join();
  consumer.join();
  updater.join();

  TEST_ASSERT_EQUAL_MESSAGE(accepted_regular.load(), executed_regular.load(),
                            "every accepted regular task must execute");
  TaskSubscription probe = task_sched.Task([]() {});
  TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(probe),
                           "pool must not stay exhausted by cancelled delayed");
  (void)task_sched.Update(Now());
}

}  // namespace ae::test_manual_task_scheduler

int test_manual_task_scheduler() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_manual_task_scheduler::test_ManualScheduler);
  RUN_TEST(ae::test_manual_task_scheduler::test_Multithread);
  RUN_TEST(ae::test_manual_task_scheduler::test_DelayedTiming);
  RUN_TEST(
      ae::test_manual_task_scheduler::test_ReclaimInactiveOnTaskWithoutUpdate);
  RUN_TEST(
      ae::test_manual_task_scheduler::test_ActiveDelayedTasksBlockAllocation);
  RUN_TEST(ae::test_manual_task_scheduler::test_ResetUpdateRestoresAllSlots);
  RUN_TEST(ae::test_manual_task_scheduler::
               test_MultithreadCancelledDelayedVsRegular);
  return UNITY_END();
}
