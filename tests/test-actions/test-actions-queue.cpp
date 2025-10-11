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
#include "aether/actions/timer_action.h"
#include "aether/actions/actions_queue.h"
#include "aether/actions/action_processor.h"
#include "aether/actions/pipeline.h"

namespace ae::test_actions_queue {

// Test action interface for flexible testing
class ITestGenAction : public Action<ITestGenAction> {
 public:
  using BaseAction = Action<ITestGenAction>;
  using BaseAction::BaseAction;

  virtual UpdateStatus Update() = 0;
  virtual void Stop() = 0;
};

// Test action implementation that can be configured for different behaviors
template <typename TBody>
class TestGenAction final : public ITestGenAction {
 public:
  using BaseAction = ITestGenAction;

  TestGenAction(ActionContext action_context, TBody&& body)
      : ITestGenAction{action_context}, body_{std::move(body)} {}

  UpdateStatus Update() override {
    if (stopped_) {
      return UpdateStatus::Stop();
    }
    return body_();
  }
  void Stop() override {
    stopped_ = true;
    BaseAction::Trigger();
  }

 private:
  TBody body_;
  bool stopped_{false};
};

void test_EmptyQueueCreation() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  // Verify queue can be created without issues
  TEST_ASSERT_TRUE(static_cast<bool>(queue));

  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Run multiple updates - empty queue should not complete
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_SingleStageExecution() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int execution_counter = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push a single successful stage
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    execution_counter = 1;
    return UpdateStatus::Result();
  }));

  // Run multiple updates to let the stage execute
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify stage executed and no completion events were triggered
  TEST_ASSERT_EQUAL(1, execution_counter);
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_MultipleStageSequentialExecution() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int execution_counter1 = 0;
  int execution_counter2 = 0;
  int execution_counter3 = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push multiple stages in order
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    execution_counter1 = 1;
    return UpdateStatus::Result();
  }));
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    execution_counter2 = 2;
    return UpdateStatus::Result();
  }));
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    execution_counter3 = 3;
    return UpdateStatus::Result();
  }));

  // Run multiple updates to let all stages execute
  for (int i = 0; i < 30; i++) {
    ap.Update(Now());
  }

  // Verify all stages executed in FIFO order
  TEST_ASSERT_EQUAL(1, execution_counter1);
  TEST_ASSERT_EQUAL(2, execution_counter2);
  TEST_ASSERT_EQUAL(3, execution_counter3);

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_StageFailureResilience() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int success_counter = 0;
  int failure_counter = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push mix of successful and failing stages using TestGenAction
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    success_counter++;
    return UpdateStatus::Result();
  }));

  queue->Push(Stage<TestGenAction>(ap, [&]() {
    failure_counter++;
    return UpdateStatus::Error();
  }));

  queue->Push(Stage<TestGenAction>(ap, [&]() {
    success_counter++;
    return UpdateStatus::Result();
  }));

  // Run multiple updates to let all stages execute
  for (int i = 0; i < 30; i++) {
    ap.Update(Now());
  }

  // Verify all stages were attempted regardless of outcome
  TEST_ASSERT_EQUAL(2, success_counter);
  TEST_ASSERT_EQUAL(1, failure_counter);

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_NullStageHandling() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int success_counter = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push stages including a null stage
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    success_counter++;
    return UpdateStatus::Result();
  }));

  // Push a null stage (return null ActionPtr)
  queue->Push(Stage([]() -> ActionPtr<ITestGenAction> { return {}; }));

  queue->Push(Stage<TestGenAction>(ap, [&]() {
    success_counter++;
    return UpdateStatus::Result();
  }));

  // Run multiple updates to let all stages execute
  for (int i = 0; i < 30; i++) {
    ap.Update(Now());
  }

  // Verify successful stages executed despite null stage
  TEST_ASSERT_EQUAL(2, success_counter);

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_MultipleConsecutiveFailures() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int failure_counter = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push multiple consecutive failing stages
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    failure_counter++;
    return UpdateStatus::Error();
  }));

  queue->Push(Stage<TestGenAction>(ap, [&]() {
    failure_counter++;
    return UpdateStatus::Error();
  }));

  queue->Push(Stage<TestGenAction>(ap, [&]() {
    failure_counter++;
    return UpdateStatus::Error();
  }));

  // Run multiple updates to let all stages execute
  for (int i = 0; i < 30; i++) {
    ap.Update(Now());
  }

  // Verify all failing stages were attempted
  TEST_ASSERT_EQUAL(3, failure_counter);

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_StopEmptyQueue() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Call Stop() on empty queue
  queue->Stop();

  // Run updates to process the stop event
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify Stop event was triggered
  TEST_ASSERT_TRUE(stop_triggered);
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
}

void test_StopDuringExecution() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  bool stage_started = false;
  bool stage_stopped = false;
  bool stage_completed = false;
  bool queue_stopped = false;

  // Push a long-running stage
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    stage_started = true;

    // This stage would normally run for a long time
    // but we'll stop it externally
    return UpdateStatus{};
  }));

  // Push additional stages that should be dropped when stopped
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    stage_completed = true;
    return UpdateStatus::Result();
  }));

  // Run a few updates to let the first stage start
  for (int i = 0; i < 5; i++) {
    ap.Update(Now());
  }

  // Verify first stage started
  TEST_ASSERT_TRUE(stage_started);
  TEST_ASSERT_FALSE(stage_completed);

  // Stop the queue while first stage is running
  queue->Stop();

  // Run updates to process the stop
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify queue was stopped and second stage was dropped
  TEST_ASSERT_FALSE(stage_completed);  // Second stage should not execute
}

void test_StopAfterStageCompletion() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  bool stage_executed = false;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push and execute a single stage
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    stage_executed = true;
    return UpdateStatus::Result();
  }));

  // Run updates to let the stage complete
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify stage executed and queue is idle
  TEST_ASSERT_TRUE(stage_executed);
  TEST_ASSERT_FALSE(stop_triggered);  // Queue should not stop automatically

  // Call Stop() on idle queue
  queue->Stop();

  // Run updates to process the stop event
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify Stop event was triggered
  TEST_ASSERT_TRUE(stop_triggered);
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
}

void test_DynamicStageAddition() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int execution_counter1 = 0;
  int execution_counter2 = 0;
  int execution_counter3 = 0;
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  // Subscribe to all completion events
  queue->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { result_triggered = true; }},
      OnError{[&]() { error_triggered = true; }},
      OnStop{[&]() { stop_triggered = true; }},
  });

  // Push initial stage
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    execution_counter1 = 1;

    // Push additional stages while first stage is executing
    queue->Push(Stage<TestGenAction>(ap, [&]() {
      execution_counter2 = 2;
      return UpdateStatus::Result();
    }));

    queue->Push(Stage<TestGenAction>(ap, [&]() {
      execution_counter3 = 3;
      return UpdateStatus::Result();
    }));

    return UpdateStatus::Result();
  }));

  // Run multiple updates to let all stages execute
  for (int i = 0; i < 30; i++) {
    ap.Update(Now());
  }

  // Verify all stages executed in correct order
  TEST_ASSERT_EQUAL(1, execution_counter1);
  TEST_ASSERT_EQUAL(2, execution_counter2);
  TEST_ASSERT_EQUAL(3, execution_counter3);

  // Verify no completion events were triggered
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_SinglePushStopCycle() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  bool stage_executed = false;
  bool stop_triggered = false;

  // Subscribe to stop event
  queue->StatusEvent().Subscribe(OnStop{[&]() { stop_triggered = true; }});

  // Push one stage and immediately stop
  queue->Push(Stage<TestGenAction>(ap, [&]() {
    stage_executed = true;
    return UpdateStatus::Result();
  }));

  // Immediately stop after pushing
  queue->Stop();

  // Run updates to process the stop
  for (int i = 0; i < 10; i++) {
    ap.Update(Now());
  }

  // Verify queue was stopped
  TEST_ASSERT_TRUE(stop_triggered);

  // Verify stage did not execute (was stopped before execution)
  TEST_ASSERT_FALSE(stage_executed);
}

void test_TimerExpirationSequence() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int completed_timers = 0;
  bool stop_triggered = false;

  // Subscribe to stop event
  queue->StatusEvent().Subscribe(OnStop{[&]() { stop_triggered = true; }});

  // Use factory functions to create timers and subscribe to their completion
  // events
  auto create_timer = [&](std::chrono::milliseconds duration,
                          int expected_order) {
    return Stage([&, duration, expected_order]() {
      auto timer = ActionPtr<TimerAction>(ap, duration);
      timer->StatusEvent().Subscribe(
          OnResult{[&, expected_order]() { completed_timers++; }});
      return timer;
    });
  };

  // Push TimerAction stages with different durations to verify FIFO order
  queue->Push(create_timer(std::chrono::milliseconds(10), 1));
  queue->Push(create_timer(std::chrono::milliseconds(20), 2));
  queue->Push(create_timer(std::chrono::milliseconds(30), 3));

  // Run updates until all timers complete
  int update_count = 0;
  auto current_time = Now();
  while (completed_timers < 3 && update_count < 100) {
    current_time += std::chrono::milliseconds(1);
    ap.Update(current_time);
    update_count++;
  }

  // Verify all timers completed in FIFO order
  TEST_ASSERT_EQUAL(3, completed_timers);
  // Verify no stop event was triggered (queue should complete normally)
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_StopBeforeTimerExpiration() {
  auto ap = ActionProcessor{};
  auto queue = MakeActionPtr<ActionsQueue>(ap);

  int expired_timers = 0;
  bool stop_triggered = false;

  // Subscribe to stop event
  queue->StatusEvent().Subscribe(OnStop{[&]() { stop_triggered = true; }});

  // Use factory functions to create timers and subscribe to their completion
  // events
  auto create_timer = [&](std::chrono::milliseconds duration) {
    return Stage([&, duration]() {
      auto timer = ActionPtr<TimerAction>(ap, duration);
      timer->StatusEvent().Subscribe(OnResult{[&]() { expired_timers++; }});
      return timer;
    });
  };

  // Push TimerAction stages with long durations
  queue->Push(create_timer(std::chrono::milliseconds(100)));
  queue->Push(create_timer(std::chrono::milliseconds(100)));
  queue->Push(create_timer(std::chrono::milliseconds(100)));

  // Run a few updates to let timers start
  auto current_time = Now();
  for (int i = 0; i < 5; i++) {
    current_time += std::chrono::milliseconds(1);
    ap.Update(current_time);
  }

  // Stop the queue before any timers expire
  queue->Stop();

  // Run updates to process the stop
  for (int i = 0; i < 10; i++) {
    current_time += std::chrono::milliseconds(1);
    ap.Update(current_time);
  }

  // Verify queue was stopped
  TEST_ASSERT_TRUE(stop_triggered);
  // Verify no timers expired (all were stopped before completion)
  TEST_ASSERT_EQUAL(0, expired_timers);
}

}  // namespace ae::test_actions_queue

int test_actions_queue() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_actions_queue::test_EmptyQueueCreation);
  RUN_TEST(ae::test_actions_queue::test_SingleStageExecution);
  RUN_TEST(ae::test_actions_queue::test_MultipleStageSequentialExecution);
  RUN_TEST(ae::test_actions_queue::test_StageFailureResilience);
  RUN_TEST(ae::test_actions_queue::test_NullStageHandling);
  RUN_TEST(ae::test_actions_queue::test_MultipleConsecutiveFailures);
  RUN_TEST(ae::test_actions_queue::test_StopEmptyQueue);
  RUN_TEST(ae::test_actions_queue::test_StopDuringExecution);
  RUN_TEST(ae::test_actions_queue::test_StopAfterStageCompletion);
  RUN_TEST(ae::test_actions_queue::test_DynamicStageAddition);
  RUN_TEST(ae::test_actions_queue::test_SinglePushStopCycle);
  RUN_TEST(ae::test_actions_queue::test_TimerExpirationSequence);
  RUN_TEST(ae::test_actions_queue::test_StopBeforeTimerExpiration);

  return UNITY_END();
}
