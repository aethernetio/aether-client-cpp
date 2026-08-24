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

#include <cstddef>
#include <variant>
#include <vector>

#include "aether/actions/action.h"
#include "aether/actions/action_pool.h"
#include "aether/actions/action_context.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae::test_action_pool {
using Scheduler = ManualTaskScheduler<TaskManagerConf<64>>;

struct TestContext {
  Scheduler& scheduler() const { return *sched; }
  Scheduler* sched;
};

class TestPoolAction final : public Action {
 public:
  TestPoolAction(int* live_counter, int* destroy_counter)
      : live_counter_{live_counter}, destroy_counter_{destroy_counter} {
    if (live_counter_ != nullptr) {
      ++(*live_counter_);
    }
  }

  ~TestPoolAction() override {
    if (live_counter_ != nullptr) {
      --(*live_counter_);
    }
    if (destroy_counter_ != nullptr) {
      ++(*destroy_counter_);
    }
  }

 private:
  int* live_counter_{nullptr};
  int* destroy_counter_{nullptr};
};

class TestPoolActionB final : public Action {
 public:
  explicit TestPoolActionB(int /*tag*/) noexcept {}
};

// Reproduces the old ActionPool bug: finishing several actions in one
// scheduler turn must not leak slots (pool_no_allocation on reuse).
void test_ActionPoolMultiFinishDrain() {
  constexpr std::size_t kCapacity = 5;
  Scheduler sched;
  TestContext ctx{&sched};
  ActionPool<TestContext, TestPoolAction, kCapacity> pool{ctx};

  int live = 0;
  int destroyed = 0;

  for (int round = 0; round < 40; ++round) {
    std::vector<TestPoolAction*> slots;
    slots.reserve(kCapacity);
    for (std::size_t i = 0; i < kCapacity; ++i) {
      auto* p = pool.Create(&live, &destroyed);
      TEST_ASSERT_NOT_NULL(p);
      slots.push_back(p);
    }
    TEST_ASSERT_EQUAL(static_cast<int>(kCapacity), live);

    // Finish at least two actions before the scheduler runs — old code
    // overwrote TaskSubscription and leaked the first destroy.
    slots[0]->Finish();
    slots[1]->Finish();
    slots[2]->Finish();
    sched.Update();
    TEST_ASSERT_EQUAL(0, pool.pending_destroy_count());
    TEST_ASSERT_FALSE(pool.drain_scheduled());
    TEST_ASSERT_EQUAL(2, live);

    // Reuse freed slots immediately.
    for (int i = 0; i < 3; ++i) {
      auto* p = pool.Create(&live, &destroyed);
      TEST_ASSERT_NOT_NULL_MESSAGE(p, "ActionPool slot leak after multi-finish");
      p->Finish();
    }
    sched.Update();
    TEST_ASSERT_EQUAL(0, pool.pending_destroy_count());
    TEST_ASSERT_EQUAL(2, live);

    // Finish remaining two from the original fill (still live).
    slots[3]->Finish();
    slots[4]->Finish();
    sched.Update();
    TEST_ASSERT_EQUAL(0, pool.pending_destroy_count());
    TEST_ASSERT_EQUAL(0, live);
  }

  TEST_ASSERT_TRUE(destroyed >= static_cast<int>(40 * kCapacity));
}

void test_VariantActionPoolMultiFinishDrain() {
  constexpr std::size_t kCapacity = 5;
  Scheduler sched;
  TestContext ctx{&sched};
  ActionPool<TestContext, std::variant<TestPoolAction, TestPoolActionB>,
             kCapacity>
      pool{ctx};

  int live = 0;
  int destroyed = 0;

  for (int round = 0; round < 20; ++round) {
    auto* a = pool.Create<TestPoolAction>(&live, &destroyed);
    auto* b = pool.Create<TestPoolActionB>(0);
    auto* c = pool.Create<TestPoolAction>(&live, &destroyed);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    a->Finish();
    b->Finish();
    c->Finish();
    sched.Update();
    TEST_ASSERT_EQUAL(0, pool.pending_destroy_count());
  }

  // Stress sequential create/finish like QueryPeerReceiveSchedule / GetCloud.
  for (int i = 0; i < 100; ++i) {
    auto* p = pool.Create<TestPoolActionB>(0);
    TEST_ASSERT_NOT_NULL_MESSAGE(
        p, "variant ActionPool exhausted during sequential stress");
    p->Finish();
    sched.Update();
    TEST_ASSERT_EQUAL(0, pool.pending_destroy_count());
  }
  TEST_ASSERT_EQUAL(0, live);
}

}  // namespace ae::test_action_pool

int test_action_pool() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_pool::test_ActionPoolMultiFinishDrain);
  RUN_TEST(ae::test_action_pool::test_VariantActionPoolMultiFinishDrain);
  return UNITY_END();
}
