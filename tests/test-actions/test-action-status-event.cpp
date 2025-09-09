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
#include "aether/actions/action_event_status.h"

namespace ae::test_action_status_event {
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

void test_StatusSelect() {
  auto ap = ActionProcessor{};

  struct Selected {
    bool result = false;
    bool error = false;
    bool stop = false;
  };

  auto test = [&](Selected const& expected) {
    auto a = ActionPtr<A>{ap};
    Selected res;
    a->StatusEvent().Subscribe([&](auto status) {
      status.OnResult([&]() { res.result = true; })
          .OnError([&]() { res.error = true; })
          .OnStop([&]() { res.stop = true; });
    });
    a->result = expected.result;
    a->error = expected.error;
    a->stop = expected.stop;
    ap.Update(Now());
    TEST_ASSERT_EQUAL(expected.result, res.result);
    TEST_ASSERT_EQUAL(expected.error, res.error);
    TEST_ASSERT_EQUAL(expected.stop, res.stop);
  };

  // select Result
  test({true, false, false});
  // select Error
  test({false, true, false});
  // select Stop
  test({false, false, true});
}

void test_CheckResultOnce() {
  auto ap = ActionProcessor{};
  // for Result
  {
    auto a = ActionPtr<A>{ap};
    // add two handlers for one state
    // one must be called
    int checked = 0;
    a->StatusEvent().Subscribe([&](auto status) {
      status.OnResult([&]() { checked++; }).OnResult([&]() { checked++; });
    });
    a->result = true;
    ap.Update(Now());
    TEST_ASSERT_EQUAL(1, checked);
  }
  // for Error
  {
    auto a = ActionPtr<A>{ap};
    int checked = 0;
    a->StatusEvent().Subscribe([&](auto status) {
      status.OnError([&]() { checked++; }).OnError([&]() { checked++; });
    });
    a->error = true;
    ap.Update(Now());
    TEST_ASSERT_EQUAL(1, checked);
  }
  // for Stop
  {
    auto a = ActionPtr<A>{ap};
    int checked = 0;
    a->StatusEvent().Subscribe([&](auto status) {
      status.OnStop([&]() { checked++; }).OnStop([&]() { checked++; });
    });
    a->stop = true;
    ap.Update(Now());
    TEST_ASSERT_EQUAL(1, checked);
  }
}

void test_ActionHandlerSelect() {
  auto ap = ActionProcessor{};
  struct Selected {
    bool result = false;
    bool error = false;
    bool stop = false;
  };

  auto test = [&](Selected selected) {
    auto a = ActionPtr<A>{ap};
    Selected res{};
    a->StatusEvent().Subscribe(
        ActionHandler{OnResult{[&]() { res.result = true; }},
                      OnError{[&]() { res.error = true; }},
                      OnStop{[&]() { res.stop = true; }}});

    a->result = selected.result;
    a->error = selected.error;
    a->stop = selected.stop;

    ap.Update(Now());
    TEST_ASSERT_EQUAL(selected.result, res.result);
    TEST_ASSERT_EQUAL(selected.error, res.error);
    TEST_ASSERT_EQUAL(selected.stop, res.stop);
  };

  // Result
  test({true, false, false});
  // Error
  test({false, true, false});
  // Stop
  test({false, false, true});
}

void test_ActionHandlerCheckResultOnce() {
  auto ap = ActionProcessor{};
  auto a = ActionPtr<A>{ap};
  [[maybe_unused]] int checked = 0;
  // must be failed
  // a->StatusEvent().Subscribe(ActionHandler{
  //     OnResult{[&]() { checked++; }},
  //     OnResult{[&]() { checked++; }},
  // });
  bool is_result = false;
  bool is_error = false;
  bool is_stop = false;
  a->StatusEvent().Subscribe(ActionHandler{
      OnResult{[&]() { is_result = true; }},
      OnError{[&]() { is_error = true; }},
      OnStop{[&]() { is_stop = true; }},
  });
  a->result = true;
  a->error = true;
  a->stop = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(0, checked);
  TEST_ASSERT_EQUAL(true, is_result);
  TEST_ASSERT_EQUAL(false, is_error);
  TEST_ASSERT_EQUAL(false, is_stop);
}

void test_ActionHandlerPartial() {
  auto ap = ActionProcessor{};
  auto a = ActionPtr<A>{ap};

  int result_count = 0;
  int stop_count = 0;

  // Only result and stop handlers
  a->StatusEvent().Subscribe(ActionHandler{OnResult{[&]() { result_count++; }},
                                           OnStop{[&]() { stop_count++; }}});

  // Trigger result
  a->result = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, result_count);
  TEST_ASSERT_EQUAL(0, stop_count);

  // Reset and trigger error (should not call any handler)
  a = ActionPtr<A>{ap};
  a->StatusEvent().Subscribe(ActionHandler{OnResult{[&]() { result_count++; }},
                                           OnStop{[&]() { stop_count++; }}});
  a->error = true;
  ap.Update(Now());
  TEST_ASSERT_EQUAL(1, result_count);
  TEST_ASSERT_EQUAL(0, stop_count);
}

}  // namespace ae::test_action_status_event

int test_action_status_event() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_action_status_event::test_StatusSelect);
  RUN_TEST(ae::test_action_status_event::test_CheckResultOnce);
  RUN_TEST(ae::test_action_status_event::test_ActionHandlerSelect);
  RUN_TEST(ae::test_action_status_event::test_ActionHandlerCheckResultOnce);
  RUN_TEST(ae::test_action_status_event::test_ActionHandlerPartial);
  return UNITY_END();
}
