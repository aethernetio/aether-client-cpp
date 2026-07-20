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

#include <string>
#include <utility>
#include <vector>

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"

#include "aether/types/data_buffer.h"

#include "assert_packet.h"

namespace ae::test_method_call {

struct Number {
  AE_REFLECT_MEMBERS(value)
  int value;
};

class ApiLevel1 : public ApiClassImpl<ApiLevel1> {
 public:
  explicit ApiLevel1(ProtocolContext& protocol_context)
      : ApiClassImpl{protocol_context}, method_3{protocol_context} {}

  void Method3Impl(float a) { method_3_event.Emit(a); }

  AE_METHODS(RegMethod<03, &ApiLevel1::Method3Impl>);

  Method<03, void(float a)> method_3;

  Event<void(float a)> method_3_event;
};

class ApiLevel0 : public ApiClassImpl<ApiLevel0> {
  class Method6Proc {
   public:
    explicit Method6Proc(ApiLevel1& api_level1) : api_{&api_level1} {}

    auto operator()(int a, SubApi<ApiLevel1> const& sub_api) {
      return DefaultArgProc{}(a, sub_api(*api_));
    }

   private:
    ApiLevel1* api_;
  };

 public:
  explicit ApiLevel0(ProtocolContext& protocol_context)
      : ApiClassImpl{protocol_context},
        api_level1{protocol_context},
        method_3{protocol_context},
        method_4{protocol_context},
        method_5{protocol_context, api_level1},
        method_6{protocol_context, Method6Proc{api_level1}},
        return_result_api{protocol_context} {}

  // methods invoked then packet received
  void Method3Impl(int a, std::string b) { method_3_event.Emit(a, b); }
  void Method4Impl(PromiseResult<Number> promise, int a) {
    method_4_event.Emit(promise.request_id, a);
  }
  void Method5Impl(SubContextImpl<ApiLevel1> sub_api, int a) {
    sub_api.Parse(api_level1);
  }
  void Method6Impl(int a, SubApiImpl<ApiLevel1> sub_api) {
    sub_api.Parse(api_level1);
  }

  ReturnResultApi return_result_api;

  AE_METHODS(RegMethod<03, &ApiLevel0::Method3Impl>,
             RegMethod<04, &ApiLevel0::Method4Impl>,
             RegMethod<05, &ApiLevel0::Method5Impl>,
             RegMethod<06, &ApiLevel0::Method6Impl>,
             ExtApi<&ApiLevel0::return_result_api>);

  // call methods to make packet
  Method<03, void(int a, std::string b)> method_3;
  Method<04, ApiPromise<Number>(int a)> method_4;
  Method<05, SubContext<ApiLevel1>(int a)> method_5;
  Method<06, void(int a, SubApi<ApiLevel1> sub), Method6Proc> method_6;

  // to signal method impl is called
  Event<void(int a, std::string const& b)> method_3_event;
  Event<void(RequestId request_id, int a)> method_4_event;

  ApiLevel1 api_level1;
};

void test_ApiMethodInvoke() {
  ProtocolContext pc;

  auto api_level0 = ApiLevel0{pc};
  auto call_context = ApiContext{api_level0};

  call_context->method_3(12, "asd");

  call_context->method_5(54)->method_3(451.F);

  DataBuffer packet = std::move(call_context);

  AssertPacket(packet, MessageId{3}, int{12}, std::string{"asd"}, MessageId{5},
               int{54}, Skip<PackedSize>{}, MessageId{3}, float{451.F});

  bool level0_method3_called = false;
  bool level1_method3_called = false;

  EventSubscriber{api_level0.method_3_event}.Subscribe(
      [&](int, std::string const&) { level0_method3_called = true; });

  EventSubscriber{api_level0.api_level1.method_3_event}.Subscribe(
      [&](int) { level1_method3_called = true; });

  // send/receive
  auto parser = ApiParser{pc, packet};
  parser.Parse(api_level0);

  TEST_ASSERT(level0_method3_called);
  TEST_ASSERT(level1_method3_called);
}

void test_ReturnResult() {
  ProtocolContext pc;

  bool promise_get_value = false;

  auto api_level0 = ApiLevel0{pc};
  auto call_context = ApiContext{api_level0};

  auto promise = call_context->method_4(42);
  promise.Subscribe([&](auto const& res) {
    if (res.IsOk()) {
      auto value = res.value().value;
      TEST_ASSERT_EQUAL(78, value);
      promise_get_value = true;
    } else {
      TEST_FAIL();
    }
  });

  DataBuffer packet = std::move(call_context);

  AssertPacket(packet, MessageId{4}, Skip<RequestId>{}, int{42});

  bool level0_method4_called = false;

  EventSubscriber{api_level0.method_4_event}.Subscribe(
      [&](RequestId req_id, int) {
        level0_method4_called = true;
        auto response_context = ApiContext{api_level0};
        response_context->return_result_api.SendResult(req_id, Number{78});
        auto data = std::move(response_context).Pack();
        auto parser = ApiParser{pc, data};
        // send/receive
        parser.Parse(api_level0);
      });

  // send/receive
  auto parser = ApiParser{pc, packet};
  parser.Parse(api_level0);

  TEST_ASSERT(level0_method4_called);
  TEST_ASSERT(promise_get_value);
}

void test_MethodWithSubApi() {
  ProtocolContext pc;

  auto api_level0 = ApiLevel0{pc};
  auto call_context = ApiContext{api_level0};

  call_context->method_6(
      12, SubApi{[&](ApiContext<ApiLevel1>& api) { api->method_3(42.12F); }});

  DataBuffer packet = std::move(call_context);
  AssertPacket(packet, MessageId{6}, int{12}, Skip<PackedSize>{}, MessageId{3},
               float{42.12F});

  bool level1_method3_called = false;

  EventSubscriber{api_level0.api_level1.method_3_event}.Subscribe(
      [&](float a) { level1_method3_called = true; });

  auto parser = ApiParser{pc, packet};
  parser.Parse(api_level0);

  TEST_ASSERT_TRUE(level1_method3_called);
}

void test_ProtocolContextStackAccess() {
  ProtocolContext pc;

  TEST_ASSERT_NULL(pc.packet_stack());
  TEST_ASSERT_NULL(pc.parser());
  TEST_ASSERT_NULL(pc.packer());

  auto packet_stack0 = PacketStack{};
  auto packet_stack1 = PacketStack{};
  pc.PushPacketStack(packet_stack0);
  TEST_ASSERT_EQUAL_PTR(&packet_stack0, pc.packet_stack());
  pc.PushPacketStack(packet_stack1);
  TEST_ASSERT_EQUAL_PTR(&packet_stack1, pc.packet_stack());
  pc.PopPacketStack();
  TEST_ASSERT_EQUAL_PTR(&packet_stack0, pc.packet_stack());
  pc.PopPacketStack();
  TEST_ASSERT_NULL(pc.packet_stack());

  auto data0 = std::vector<std::uint8_t>{};
  auto data1 = std::vector<std::uint8_t>{};
  {
    auto parser0 = ApiParser{pc, data0};
    TEST_ASSERT_EQUAL_PTR(&parser0, pc.parser());
    {
      auto parser1 = ApiParser{pc, data1};
      TEST_ASSERT_EQUAL_PTR(&parser1, pc.parser());
    }
    TEST_ASSERT_EQUAL_PTR(&parser0, pc.parser());
  }
  TEST_ASSERT_NULL(pc.parser());

  {
    auto packer0 = ApiPacker{pc, data0};
    TEST_ASSERT_EQUAL_PTR(&packer0, pc.packer());
    {
      auto packer1 = ApiPacker{pc, data1};
      TEST_ASSERT_EQUAL_PTR(&packer1, pc.packer());
    }
    TEST_ASSERT_EQUAL_PTR(&packer0, pc.packer());
  }
  TEST_ASSERT_NULL(pc.packer());
}

void test_PendingResponseCapacityEvictsOldest() {
  ProtocolContext pc;

  bool first_promise_evicted = false;

  auto api_level0 = ApiLevel0{pc};
  auto call_context = ApiContext{api_level0};
  auto subscriptions = std::vector<Subscription>{};
  subscriptions.reserve(AE_API_PROTOCOL_MAX_PENDING_RESPONSES + 1);

  for (auto i = 0U; i < AE_API_PROTOCOL_MAX_PENDING_RESPONSES + 1; ++i) {
    auto promise = call_context->method_4(static_cast<int>(i));
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

void test_PendingResponseDuplicateRequestIdReplacesOld() {
  ProtocolContext pc;

  auto request_id = RequestId{42};
  auto first_promise = ApiPromise<short>{pc, request_id};
  auto first_evicted = false;
  auto first_subscription = first_promise.Subscribe([&](auto const& res) {
    TEST_ASSERT_FALSE(res.IsOk());
    TEST_ASSERT_EQUAL(-1, res.error());
    first_evicted = true;
  });

  auto second_promise = ApiPromise<float>{pc, request_id};
  auto second_subscription = second_promise.Subscribe([](auto const&) {});

  static_cast<void>(first_subscription);
  static_cast<void>(second_subscription);
  TEST_ASSERT_TRUE(first_evicted);
}

void test_PendingResponseFreshSameTypeReplacesOld() {
  ProtocolContext pc;

  auto request_id = RequestId{42};
  auto first_promise = ApiPromise<short>{pc, request_id};
  auto first_evicted = false;
  auto first_subscription = first_promise.Subscribe([&](auto const& res) {
    TEST_ASSERT_FALSE(res.IsOk());
    TEST_ASSERT_EQUAL(-1, res.error());
    first_evicted = true;
  });

  auto second_promise = ApiPromise<short>{pc, request_id};
  auto second_subscription = second_promise.Subscribe([](auto const&) {});

  static_cast<void>(first_subscription);
  static_cast<void>(second_subscription);
  TEST_ASSERT_TRUE(first_evicted);
}

void test_PendingResponseCompletionFreesPoolSlot() {
  ProtocolContext pc;

  auto second_promise_evicted = false;
  auto subscriptions = std::vector<Subscription>{};
  subscriptions.reserve(AE_API_PROTOCOL_MAX_PENDING_RESPONSES + 1);

  for (auto i = 0U; i < AE_API_PROTOCOL_MAX_PENDING_RESPONSES; ++i) {
    auto promise = ApiPromise<short>{pc, RequestId{i + 1}};
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

  auto extra_promise = ApiPromise<short>{pc, RequestId{1000}};
  subscriptions.emplace_back(extra_promise.Subscribe([](auto const&) {}));

  TEST_ASSERT_FALSE(second_promise_evicted);
}

void test_PendingResponseFifoAfterMiddleRemoval() {
  ProtocolContext pc;

  auto first_evicted = false;
  auto second_evicted = false;
  auto subscriptions = std::vector<Subscription>{};
  subscriptions.reserve(AE_API_PROTOCOL_MAX_PENDING_RESPONSES + 2);

  for (auto i = 0U; i < AE_API_PROTOCOL_MAX_PENDING_RESPONSES; ++i) {
    auto id = i + 1U;
    auto promise = ApiPromise<short>{pc, RequestId{id}};
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

  auto fill_promise = ApiPromise<short>{pc, RequestId{1000}};
  subscriptions.emplace_back(fill_promise.Subscribe([](auto const&) {}));
  auto overflow_promise = ApiPromise<short>{pc, RequestId{1001}};
  subscriptions.emplace_back(overflow_promise.Subscribe([](auto const&) {}));

  TEST_ASSERT_TRUE(first_evicted);
  TEST_ASSERT_FALSE(second_evicted);
}

void test_PendingResponseDuplicateReplacementPreservesFifo() {
  ProtocolContext pc;

  auto first_evicted = false;
  auto second_evicted = false;
  auto third_evicted = false;
  auto subscriptions = std::vector<Subscription>{};
  subscriptions.reserve(AE_API_PROTOCOL_MAX_PENDING_RESPONSES + 2);

  for (auto i = 0U; i < AE_API_PROTOCOL_MAX_PENDING_RESPONSES; ++i) {
    auto id = i + 1U;
    auto promise = ApiPromise<short>{pc, RequestId{id}};
    auto subscription = promise.Subscribe([&, id](auto const& res) {
      if (!res.IsOk() && (res.error() == static_cast<std::uint32_t>(-1))) {
        if (id == 1U) {
          first_evicted = true;
        } else if (id == 2U) {
          second_evicted = true;
        } else if (id == 3U) {
          third_evicted = true;
        }
      }
    });
    subscriptions.emplace_back(std::move(subscription));
  }

  auto replacement_promise = ApiPromise<short>{pc, RequestId{3}};
  subscriptions.emplace_back(replacement_promise.Subscribe([](auto const&) {}));
  TEST_ASSERT_TRUE(third_evicted);

  auto overflow_promise = ApiPromise<short>{pc, RequestId{1000}};
  subscriptions.emplace_back(overflow_promise.Subscribe([](auto const&) {}));

  TEST_ASSERT_TRUE(first_evicted);
  TEST_ASSERT_FALSE(second_evicted);
}

}  // namespace ae::test_method_call

int test_method_call() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_method_call::test_ApiMethodInvoke);
  RUN_TEST(ae::test_method_call::test_ReturnResult);
  RUN_TEST(ae::test_method_call::test_MethodWithSubApi);
  RUN_TEST(ae::test_method_call::test_ProtocolContextStackAccess);
  RUN_TEST(ae::test_method_call::test_PendingResponseCapacityEvictsOldest);
  RUN_TEST(
      ae::test_method_call::test_PendingResponseDuplicateRequestIdReplacesOld);
  RUN_TEST(ae::test_method_call::test_PendingResponseFreshSameTypeReplacesOld);
  RUN_TEST(ae::test_method_call::test_PendingResponseCompletionFreesPoolSlot);
  RUN_TEST(ae::test_method_call::test_PendingResponseFifoAfterMiddleRemoval);
  RUN_TEST(ae::test_method_call::
               test_PendingResponseDuplicateReplacementPreservesFifo);
  return UNITY_END();
}
