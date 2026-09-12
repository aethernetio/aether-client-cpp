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

  virtual ApiPromise<short> Method3(int value) = 0;

  API_LIST(METHOD(3, Method3))
};

class PendingResponsesClientApi : public PendingResponsesApi {
 public:
  ApiPromise<short> Method3(int value) override {
    return ClientMethod<&PendingResponsesApi::Method3>(value);
  }
};

struct PendingResponsesFixture {
  EventSystem event_system;
  TaskScheduler scheduler;
  ProtocolContext protocol_context{scheduler, event_system};
};

void test_RequestIdsStartIndependentlyAndAreSequential() {
  PendingResponsesFixture first;
  PendingResponsesFixture second;

  TEST_ASSERT_EQUAL_UINT32(0, first.protocol_context.NextRequestId().id);
  TEST_ASSERT_EQUAL_UINT32(1, first.protocol_context.NextRequestId().id);
  TEST_ASSERT_EQUAL_UINT32(0, second.protocol_context.NextRequestId().id);
  TEST_ASSERT_EQUAL_UINT32(1, second.protocol_context.NextRequestId().id);
}

void test_ApiPromiseBoolReflectsWhetherFutureIsPresent() {
  PendingResponsesFixture fxtr;

  auto empty_value = ApiPromise<short>{};
  auto filled_value = ApiPromise<short>{
      fxtr.protocol_context.CreateFuture<short>(RequestId{1})};
  auto empty_void = ApiPromise<void>{};
  auto filled_void =
      ApiPromise<void>{fxtr.protocol_context.CreateFuture<void>(RequestId{2})};

  TEST_ASSERT_FALSE(static_cast<bool>(empty_value));
  TEST_ASSERT_TRUE(static_cast<bool>(filled_value));
  TEST_ASSERT_FALSE(static_cast<bool>(empty_void));
  TEST_ASSERT_TRUE(static_cast<bool>(filled_void));
}

void test_CapacityEvictsOldest() {
  PendingResponsesFixture fxtr;
  auto& pc = fxtr.protocol_context;

  bool first_promise_evicted = false;
  auto client_api = PendingResponsesClientApi{};
  auto call_context = ApiContext{client_api, pc};
  auto subscriptions = std::vector<Subscription>{};

  for (auto i = 0U; i < ProtocolContext::kMaxPendingResponses + 1; ++i) {
    auto promise = call_context->Method3(static_cast<int>(i));
    auto subscription = promise.Subscribe([&, i](auto const& res) {
      if (i == 0U) {
        TEST_ASSERT_FALSE(res.IsOk());
        TEST_ASSERT_EQUAL(-1, res.error());
        first_promise_evicted = true;
      }
    });
    subscriptions.emplace_back(std::move(subscription));
  }

  TEST_ASSERT_TRUE(first_promise_evicted);
}

void test_CompletionFreesPoolSlot() {
  PendingResponsesFixture fxtr;
  auto& pc = fxtr.protocol_context;

  auto second_promise_evicted = false;
  auto subscriptions = std::vector<Subscription>{};

  for (auto i = 0U; i < ProtocolContext::kMaxPendingResponses; ++i) {
    auto promise = ApiPromise<short>{pc.CreateFuture<short>(RequestId{i + 1})};

    auto subscription = promise.Subscribe([&, i](auto const& res) {
      if (i == 1U) {
        TEST_ASSERT_FALSE(res.IsOk());
        TEST_ASSERT_EQUAL(static_cast<std::uint32_t>(-1), res.error());
        second_promise_evicted = true;
      }
    });
    subscriptions.emplace_back(std::move(subscription));
  }

  pc.SetSendErrorResponse(RequestId{1}, 0, 7);

  auto extra_promise = ApiPromise<short>{
      fxtr.protocol_context.CreateFuture<short>(RequestId{1000})};
  subscriptions.emplace_back(extra_promise.Subscribe([](auto const&) {}));

  TEST_ASSERT_FALSE(second_promise_evicted);
}

void test_FifoAfterMiddleRemoval() {
  PendingResponsesFixture fxtr;
  auto& pc = fxtr.protocol_context;

  auto first_evicted = false;
  auto second_evicted = false;
  auto subscriptions = std::vector<Subscription>{};

  for (auto i = 0U; i < ProtocolContext::kMaxPendingResponses; ++i) {
    auto id = i + 1U;
    auto promise = ApiPromise<short>{pc.CreateFuture<short>(RequestId{id})};
    auto subscription = promise.Subscribe([&, id](auto const& res) {
      if (!res.IsOk() && (res.error() == static_cast<std::uint32_t>(-1))) {
        if (id == 1U) {
          first_evicted = true;
        } else if (id == 2U) {
          second_evicted = true;
        }
      }
    });
    subscriptions.emplace_back(std::move(subscription));
  }

  pc.SetSendErrorResponse(RequestId{3}, 0, 7);

  auto fill_promise =
      ApiPromise<short>{pc.CreateFuture<short>(RequestId{1000})};
  subscriptions.emplace_back(fill_promise.Subscribe([](auto const&) {}));
  auto overflow_promise =
      ApiPromise<short>{pc.CreateFuture<short>(RequestId{1001})};
  subscriptions.emplace_back(overflow_promise.Subscribe([](auto const&) {}));

  TEST_ASSERT_TRUE(first_evicted);
  TEST_ASSERT_FALSE(second_evicted);
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
