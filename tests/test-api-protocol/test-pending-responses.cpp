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

#include <vector>

#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/api_protocol/api_protocol.h"

namespace ae::test_api_protocol_pending_responses {


class PendingResponsesApi : public DeclareApi<PendingResponsesApi> {
 public:
  virtual ~PendingResponsesApi() = default;

  virtual void Method3(int value) = 0;

  API_LIST(METHOD(3, Method3))
};

class PendingResponsesClientApi : public PendingResponsesApi {
 public:
  void Method3(int value) override {
    ClientMethod<&PendingResponsesApi::Method3>(value);
  }
};


struct PendingResponsesFixture {
  EventSystem event_system;
  TaskScheduler scheduler;
  ProtocolContext protocol_context{scheduler, event_system};
};


void test_RequestIdsStartIndependentlyAndAreSequential() {
  // TODO(push-model): request_id generation was removed.
}



void test_ApiPromiseBoolReflectsWhetherFutureIsPresent() {
  // TODO(push-model): ApiPromise and CreateFuture were removed.
}



void test_CapacityEvictsOldest() {
  // TODO(push-model): pending responses pool was removed.
}



void test_CompletionFreesPoolSlot() {
  // TODO(push-model): pending responses pool was removed.
}



void test_FifoAfterMiddleRemoval() {
  // TODO(push-model): pending responses pool was removed.
}


}  // namespace ae::test_api_protocol_pending_responses

int test_api_protocol_pending_responses() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_pending_responses;  // NOLINT

  RUN_TEST(test_CapacityEvictsOldest);
  RUN_TEST(test_RequestIdsStartIndependentlyAndAreSequential);
  RUN_TEST(test_ApiPromiseBoolReflectsWhetherFutureIsPresent);
  RUN_TEST(test_CompletionFreesPoolSlot);
  RUN_TEST(test_FifoAfterMiddleRemoval);
  return UNITY_END();
}