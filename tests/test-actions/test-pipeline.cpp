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
#include "aether/actions/pipeline.h"
#include "aether/actions/gen_action.h"
#include "aether/actions/timer_action.h"
#include "aether/actions/action_processor.h"

namespace ae::test_pipeline {
struct Success {
  UpdateStatus operator()() { return UpdateStatus::Result(); }
};

struct Failure {
  UpdateStatus operator()() { return UpdateStatus::Error(); }
};

struct Stop {
  UpdateStatus operator()() { return UpdateStatus::Stop(); }
};

void test_BasicPipelineExecution() {
  auto ap = ActionProcessor{};

  auto pipeline = MakeActionPtr<Pipeline>(ap, Stage<GenAction>(ap, Success{}),
                                          Stage<GenAction>(ap, Success{}));

  TEST_ASSERT_EQUAL(0, pipeline->index());
  TEST_ASSERT_EQUAL(2, pipeline->count());

  bool success = false;
  bool failed = false;
  bool stopped = false;

  pipeline->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { success = true; }},
      OnError{[&]() { failed = true; }},
      OnStop{[&]() { stopped = true; }},
  });

  while (!(success || failed || stopped)) {
    ap.Update(Now());
  }

  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_FALSE(failed);
  TEST_ASSERT_FALSE(stopped);
}

void test_Fails() {
  auto ap = ActionProcessor{};

  // the first stage fails
  {
    auto pipeline = MakeActionPtr<Pipeline>(ap, Stage<GenAction>(ap, Failure{}),
                                            Stage<GenAction>(ap, Success{}));

    bool success = false;
    bool failed = false;
    bool stopped = false;

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[&]() { success = true; }},
        OnError{[&]() { failed = true; }},
        OnStop{[&]() { stopped = true; }},
    });

    while (!(success || failed || stopped)) {
      ap.Update(Now());
    }

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_TRUE(failed);
    TEST_ASSERT_FALSE(stopped);
  }

  // the last stage fails
  {
    auto pipeline = MakeActionPtr<Pipeline>(ap, Stage<GenAction>(ap, Success{}),
                                            Stage<GenAction>(ap, Success{}),
                                            Stage<GenAction>(ap, Failure{}));

    bool success = false;
    bool failed = false;
    bool stopped = false;

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[&]() { success = true; }},
        OnError{[&]() { failed = true; }},
        OnStop{[&]() { stopped = true; }},
    });

    while (!(success || failed || stopped)) {
      ap.Update(Now());
    }

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_TRUE(failed);
    TEST_ASSERT_FALSE(stopped);
  }
}

void test_StageStopped() {
  auto ap = ActionProcessor{};
  // the first stage is stopped
  {
    auto pipeline = MakeActionPtr<Pipeline>(ap, Stage<GenAction>(ap, Stop{}),
                                            Stage<GenAction>(ap, Success{}));

    bool success = false;
    bool failed = false;
    bool stopped = false;

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[&]() { success = true; }},
        OnError{[&]() { failed = true; }},
        OnStop{[&]() { stopped = true; }},
    });

    while (!(success || failed || stopped)) {
      ap.Update(Now());
    }

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_FALSE(failed);
    TEST_ASSERT_TRUE(stopped);
  }
  // the last stage is stopped
  {
    auto pipeline = MakeActionPtr<Pipeline>(ap, Stage<GenAction>(ap, Success{}),
                                            Stage<GenAction>(ap, Success{}),
                                            Stage<GenAction>(ap, Stop{}));

    bool success = false;
    bool failed = false;
    bool stopped = false;

    pipeline->StatusEvent().Subscribe(ActionHandler{
        OnResult{[&]() { success = true; }},
        OnError{[&]() { failed = true; }},
        OnStop{[&]() { stopped = true; }},
    });

    while (!(success || failed || stopped)) {
      ap.Update(Now());
    }

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_FALSE(failed);
    TEST_ASSERT_TRUE(stopped);
  }
}

void test_PipelineStopped() {
  auto ap = ActionProcessor{};

  bool timer_stopped = false;
  bool timer_expired = false;

  auto pipeline = MakeActionPtr<Pipeline>(
      ap,                               //
      Stage<GenAction>(ap, Success{}),  //
      Stage([&]() {
        auto timer = ActionPtr<TimerAction>(ap, std::chrono::seconds{1});
        timer->StatusEvent().Subscribe(ActionHandler{
            OnResult{[&]() { timer_expired = true; }},
            OnStop{[&]() { timer_stopped = true; }},
        });
        return timer;
      }));

  bool stopped = false;
  pipeline->StatusEvent().Subscribe(OnStop{[&]() { stopped = true; }});

  ap.Update(Now());
  ap.Update(Now());

  TEST_ASSERT_EQUAL(1, pipeline->index());

  pipeline->Stop();
  ap.Update(Now());
  ap.Update(Now());

  TEST_ASSERT_TRUE(stopped);
  TEST_ASSERT_TRUE(timer_stopped);
  TEST_ASSERT_FALSE(timer_expired);
}

void test_NullStage() {
  auto ap = ActionProcessor{};

  auto pipeline = MakeActionPtr<Pipeline>(
      ap,  //
      Stage<GenAction>(ap, Success{}),
      // return null
      Stage([]() -> ActionPtr<GenAction<Success>> { return {}; }));

  bool success = false;
  bool failed = false;
  bool stopped = false;

  pipeline->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { success = true; }},
      OnError{[&]() { failed = true; }},
      OnStop{[&]() { stopped = true; }},
  });

  while (!(success || failed || stopped)) {
    ap.Update(Now());
  }

  TEST_ASSERT_FALSE(success);
  TEST_ASSERT_TRUE(failed);
  TEST_ASSERT_FALSE(stopped);
}

}  // namespace ae::test_pipeline

int test_pipeline() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_pipeline::test_BasicPipelineExecution);
  RUN_TEST(ae::test_pipeline::test_Fails);
  RUN_TEST(ae::test_pipeline::test_StageStopped);
  RUN_TEST(ae::test_pipeline::test_PipelineStopped);
  RUN_TEST(ae::test_pipeline::test_NullStage);
  return UNITY_END();
}
