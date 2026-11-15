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

#pragma once

#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
#include "aether-miscpp/types/result.h"
#include "aether/executors/executors.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/types/address.h"

namespace ae::cares_query_completion_internal {
// The resolver owns a C callback until completion, independently of the sender
// operation. Only the model scheduler may invoke (or destroy) its receiver.
template <typename Receiver>
class QueryCompletion {
 public:
  using ResultType = Result<std::vector<Endpoint>, int>;
  struct State {
    std::mutex mutex;
    QueryCompletion* owner{};
    TaskScheduler* scheduler{};
    TaskSubscription task;
    std::optional<ResultType> result;
  };
  // c-ares releases this state on its event thread; use atomic shared
  // ownership.
  using StatePtr = std::shared_ptr<State>;

  QueryCompletion(Receiver receiver, TaskScheduler& scheduler)
      : receiver_{std::move(receiver)}, state_{std::make_shared<State>()} {
    state_->owner = this;
    state_->scheduler = &scheduler;
  }
  QueryCompletion(QueryCompletion const&) = delete;
  QueryCompletion& operator=(QueryCompletion const&) = delete;
  ~QueryCompletion() {
    auto lock = std::scoped_lock{state_->mutex};
    state_->owner = nullptr;
    state_->scheduler->Cancel(state_->task);
    state_->scheduler = nullptr;
  }
  StatePtr const& state() const { return state_; }

  static void Deliver(StatePtr state, ResultType result) {
    auto lock = std::scoped_lock{state->mutex};
    if (state->owner == nullptr) {
      return;
    }
    // Binding the subscription under the scheduler lock closes the race with
    // task reclamation. Operation destruction cancels an already queued result.
    state->result.emplace(std::move(result));
    bool const posted = state->scheduler->Task(state->task, [state]() {
      // Delivery and operation destruction run on the same model thread.
      // A completion is permitted to destroy its own operation.
      auto* owner = state->owner;
      auto& result = *state->result;
      if (result.IsOk()) {
        stdexec::set_value(std::move(owner->receiver_),
                           std::move(result.value()));
      } else {
        stdexec::set_error(std::move(owner->receiver_), result.error());
      }
    });
    if (!posted) {
      std::abort();
    }
  }

 private:
  Receiver receiver_;
  StatePtr state_;
};
}  // namespace ae::cares_query_completion_internal

#if AE_TESTS
#  include <functional>
#  include <thread>
#  include "tests/inline.h"

namespace tests::cares_query_completion {
struct Receiver {
  using receiver_concept = stdexec::receiver_t;
  int* values;
  int* errors;
  std::function<void()>* on_value{};
  auto get_env() const noexcept { return stdexec::env<>{}; }
  void set_value(std::vector<ae::Endpoint>&&) && noexcept {
    ++*values;
    if (on_value != nullptr) {
      (*on_value)();
    }
  }
  void set_error(int error) && noexcept { *errors = error; }
};
using Completion =
    ae::cares_query_completion_internal::QueryCompletion<Receiver>;
AE_TEST_INLINE(test_CaresLateCallbackAfterOperationAndSchedulerDestruction) {
  int values = 0, errors = 0;
  Completion::StatePtr pending;
  {
    ae::TaskScheduler scheduler;
    {
      Completion operation{Receiver{&values, &errors}, scheduler};
      pending = operation.state();
    }
  }
  Completion::Deliver(pending, ae::Error{16});  // ares_destroy's late callback
  TEST_ASSERT_EQUAL(0, values);
  TEST_ASSERT_EQUAL(0, errors);
}
AE_TEST_INLINE(test_CaresQueuedCompletionIsCancelled) {
  ae::TaskScheduler scheduler;
  int values = 0, errors = 0;
  {
    Completion operation{Receiver{&values, &errors}, scheduler};
    Completion::Deliver(operation.state(), ae::Error{7});
  }
  scheduler.Update();
  TEST_ASSERT_EQUAL(0, values);
  TEST_ASSERT_EQUAL(0, errors);
}
AE_TEST_INLINE(test_CaresCompletionUsesOwnerScheduler) {
  ae::TaskScheduler scheduler;
  int values = 0, errors = 0;
  Completion operation{Receiver{&values, &errors}, scheduler};
  std::thread callback{[state = operation.state()] {
    Completion::Deliver(state, ae::Ok{std::vector<ae::Endpoint>{}});
  }};
  callback.join();
  TEST_ASSERT_EQUAL(0, values);
  scheduler.Update();
  TEST_ASSERT_EQUAL(1, values);
  TEST_ASSERT_EQUAL(0, errors);
}
AE_TEST_INLINE(test_CaresLiveErrorDeliveredOnce) {
  ae::TaskScheduler scheduler;
  int values = 0, errors = 0;
  Completion operation{Receiver{&values, &errors}, scheduler};
  Completion::Deliver(operation.state(), ae::Error{7});
  scheduler.Update();
  scheduler.Update();
  TEST_ASSERT_EQUAL(0, values);
  TEST_ASSERT_EQUAL(7, errors);
}
AE_TEST_INLINE(test_CaresReceiverCanDestroyItsOperation) {
  ae::TaskScheduler scheduler;
  int values = 0, errors = 0;
  std::unique_ptr<Completion> operation;
  std::function<void()> done = [&] { operation.reset(); };
  operation = std::make_unique<Completion>(Receiver{&values, &errors, &done},
                                           scheduler);
  Completion::Deliver(operation->state(), ae::Ok{std::vector<ae::Endpoint>{}});
  scheduler.Update();
  TEST_ASSERT_NULL(operation.get());
  TEST_ASSERT_EQUAL(1, values);
}
AE_TEST_INLINE(test_CaresCallbackRacesCancellation) {
  ae::TaskScheduler scheduler;
  int values = 0, errors = 0;
  for (int i = 0; i < 200; ++i) {
    auto operation =
        std::make_unique<Completion>(Receiver{&values, &errors}, scheduler);
    std::thread callback{[state = operation->state()] {
      Completion::Deliver(state, ae::Error{7});
    }};
    operation.reset();
    callback.join();
    scheduler.Update();
  }
  TEST_ASSERT_EQUAL(0, values);
  TEST_ASSERT_EQUAL(0, errors);
}
}  // namespace tests::cares_query_completion
#endif
