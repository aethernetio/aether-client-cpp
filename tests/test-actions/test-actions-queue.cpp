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

#include <utility>
#include <variant>

#include "aether/actions/action2_.h"
#include "aether/actions/actions_queue.h"
#include "aether/actions/action_context.h"
#include "aether/tasks/manual_task_scheduler.h"

namespace ae::test_actions_queue {
using Scheduler = ManualTaskScheduler<TaskManagerConf<50>>;

struct TestContext {
  Scheduler& scheduler() const { return *sched; }
  Scheduler* sched;
};

template <typename TBody>
class TestSyncGenAction final : public a2::Action {
 public:
  TestSyncGenAction(ActionContext auto const&, TBody&& body) {
    std::invoke(std::move(body));
    Finish();
  }
};

template <typename TBody>
class TestAsyncGenAction final : public a2::Action {
 public:
  TestAsyncGenAction(ActionContext auto const& context, TBody&& body)
      : body_{std::move(body)} {
    context.scheduler().Task([&]() {
      if (!stopped_) {
        std::invoke(body_);
      }
      Finish();
    });
  }

  void Stop() { stopped_ = true; }

 private:
  bool stopped_{false};
  TBody body_;
};

template <typename TGenAction, typename Gen>
class TestGenActionRunner {
 public:
  explicit TestGenActionRunner(Gen&& gen) : store_{std::move(gen)} {}
  TestGenActionRunner(TestGenActionRunner&& other) noexcept
      : store_{std::move(std::get<Gen>(other.store_))} {}

  TGenAction* operator()() {
    auto gen = std::move(std::get<Gen>(store_));
    std::invoke(gen, [&]<typename... Args>(Args&&... args) {
      store_.template emplace<TGenAction>(std::forward<Args>(args)...);
    });

    return &std::get<TGenAction>(store_);
  }

 private:
  std::variant<Gen, TGenAction> store_;
};

template <template <typename> typename TAction, typename Func>
auto Stage(ActionContext auto const& context, Func&& body) {
  auto gen = [c_{context},
              b_{std::forward<Func>(body)}]<typename C_>(C_&& closure) mutable {
    std::invoke(std::forward<C_>(closure), c_, std::move(b_));
  };
  return TestGenActionRunner<TAction<Func>, decltype(gen)>{std::move(gen)};
}

void test_SingleStageExecution() {
  Scheduler sched;
  auto queue = ActionsQueue{};

  int execution_counter = 0;
  // Push a single successful stage
  queue.Push(Stage<TestSyncGenAction>(TestContext{&sched},
                                      [&]() { execution_counter++; }));
  // stage already executed
  TEST_ASSERT_EQUAL(1, execution_counter);
}

void test_MultipleStageSequentialExecution() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  int execution_counter1 = 0;
  int execution_counter2 = 0;
  int execution_counter3 = 0;

  // Push multiple stages in order
  queue.Push(
      Stage<TestAsyncGenAction>(context, [&]() { execution_counter1 = 1; }));
  queue.Push(
      Stage<TestAsyncGenAction>(context, [&]() { execution_counter2 = 2; }));
  // last is sync
  queue.Push(
      Stage<TestSyncGenAction>(context, [&]() { execution_counter3 = 3; }));

  // Verify all stages executed in FIFO order
  // first stage already started
  // run update to execute its body and start next stage
  // run update to executed 2nd stage body, start next stage which is
  // synchronouse and would be executed immediately
  sched.Update();
  TEST_ASSERT_EQUAL(1, execution_counter1);
  TEST_ASSERT_EQUAL(0, execution_counter2);

  sched.Update();
  TEST_ASSERT_EQUAL(2, execution_counter2);
  TEST_ASSERT_EQUAL(3, execution_counter3);
}

void test_NullStageHandling() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  int success_counter = 0;

  // Push stages including a null stage
  queue.Push(Stage<TestAsyncGenAction>(context, [&]() { success_counter++; }));
  // Push a null stage (return null action)
  queue.Push(ae::Stage([]() -> a2::Action* { return {}; }));

  queue.Push(Stage<TestAsyncGenAction>(context, [&]() { success_counter++; }));

  // Run multiple updates to let all stages execute
  sched.Update();
  TEST_ASSERT_EQUAL(1, success_counter);
  sched.Update();
  // Verify successful stages executed despite null stage
  TEST_ASSERT_EQUAL(2, success_counter);
}

void test_StopEmptyQueue() {
  auto queue = ActionsQueue{};

  // Call Stop() on empty queue
  queue.Stop();
  // nothing actually happens
}

void test_StopDuringExecution() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  bool stage_started = false;
  bool stage_completed = false;

  // Push a long-running stage
  queue.Push(
      Stage<TestAsyncGenAction>(context, [&]() { stage_started = true; }));

  // Push additional stages that should be dropped when stopped
  queue.Push(
      Stage<TestAsyncGenAction>(context, [&]() { stage_completed = true; }));

  sched.Update();

  // Verify first stage started
  TEST_ASSERT_TRUE(stage_started);
  TEST_ASSERT_FALSE(stage_completed);

  // Stop the queue while first stage is running
  queue.Stop();

  sched.Update();
  // Verify queue was stopped and second stage was dropped
  TEST_ASSERT_FALSE(stage_completed);  // Second stage should not execute
}

void test_StopAfterStageCompletion() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  bool stage_executed = false;

  // Push and execute a single stage
  queue.Push(
      Stage<TestAsyncGenAction>(context, [&]() { stage_executed = true; }));

  // Run updates to let the stage complete
  sched.Update();

  // Verify stage executed and queue is idle
  TEST_ASSERT_TRUE(stage_executed);

  // Call Stop() on idle queue
  queue.Stop();
  // nothing happened
  stage_executed = false;
  // can add new stage
  queue.Push(
      Stage<TestSyncGenAction>(context, [&]() { stage_executed = true; }));
  TEST_ASSERT_TRUE(stage_executed);
}

void test_DynamicStageAddition() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  int execution_counter1 = 0;
  int execution_counter2 = 0;
  int execution_counter3 = 0;

  // Push initial stage
  queue.Push(Stage<TestAsyncGenAction>(context, [&]() {
    execution_counter1 = 1;

    // Push additional stages while first stage is executing
    queue.Push(
        Stage<TestSyncGenAction>(context, [&]() { execution_counter2 = 2; }));
    queue.Push(
        Stage<TestSyncGenAction>(context, [&]() { execution_counter3 = 3; }));
  }));

  sched.Update();

  // Verify all stages executed in correct order
  TEST_ASSERT_EQUAL(1, execution_counter1);
  TEST_ASSERT_EQUAL(2, execution_counter2);
  TEST_ASSERT_EQUAL(3, execution_counter3);
}

void test_SinglePushStopCycle() {
  Scheduler sched;
  auto context = TestContext{&sched};

  auto queue = ActionsQueue{};

  bool stage_executed = false;

  // Push one stage and immediately stop
  queue.Push(Stage<TestAsyncGenAction>(context, [&]() {
    // Push additional stages while first stage is executing
    queue.Push(
        Stage<TestSyncGenAction>(context, [&]() { stage_executed = true; }));
    // stop after pushing, sync test must not be executed
    queue.Stop();
  }));

  sched.Update();

  // Verify stage did not execute (was stopped before execution)
  TEST_ASSERT_FALSE(stage_executed);
}
}  // namespace ae::test_actions_queue

int test_actions_queue() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_actions_queue::test_SingleStageExecution);
  RUN_TEST(ae::test_actions_queue::test_MultipleStageSequentialExecution);
  RUN_TEST(ae::test_actions_queue::test_NullStageHandling);
  RUN_TEST(ae::test_actions_queue::test_StopEmptyQueue);
  RUN_TEST(ae::test_actions_queue::test_StopDuringExecution);
  RUN_TEST(ae::test_actions_queue::test_StopAfterStageCompletion);
  RUN_TEST(ae::test_actions_queue::test_DynamicStageAddition);
  RUN_TEST(ae::test_actions_queue::test_SinglePushStopCycle);

  return UNITY_END();
}
