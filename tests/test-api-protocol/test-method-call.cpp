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
#include <iostream>

#include "aether/events/events.h"
#include "aether/actions/action_processor.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/protocol_context.h"
#include "aether/api_protocol/return_result_api.h"

#include "aether/types/data_buffer.h"

#include "aether/tele/tele_init.h"

#include "assert_packet.h"

namespace ae::test_method_call {

class ApiLevel1 : public ApiClassImpl<ApiLevel1> {
 public:
  explicit ApiLevel1(ProtocolContext& protocol_context)
      : method_3{protocol_context} {}

  void Method3Impl(ApiParser&, float a) { method_3_event.Emit(a); }

  using ApiMethods = ImplList<RegMethod<03, &ApiLevel1::Method3Impl>>;

  Method<03, void(float a)> method_3;

  Event<void(float a)> method_3_event;
};

class ApiLevel0 : public ReturnResultApiImpl,
                  public ApiClassImpl<ApiLevel0, ReturnResultApiImpl> {
 public:
  ApiLevel0(ProtocolContext& protocol_context, ActionContext action_context)
      : ReturnResultApiImpl{protocol_context},
        api_level1{protocol_context},
        method_3{protocol_context},
        method_4{protocol_context, action_context},
        method_5{protocol_context, api_level1} {}

  // methods invoked then packet received
  void Method3Impl(ApiParser&, int a, std::string b) {
    method_3_event.Emit(a, b);
  }
  void Method4Impl(ApiParser& parser, PromiseResult<int> promise, int a) {
    method_4_event.Emit(promise.request_id, a);
  }
  void Method5Impl(ApiParser& parser, SubApi<ApiLevel1> sub_api, int a) {
    sub_api.Parse(api_level1);
  }

  using ApiMethods = ImplList<RegMethod<03, &ApiLevel0::Method3Impl>,
                              RegMethod<04, &ApiLevel0::Method4Impl>,
                              RegMethod<05, &ApiLevel0::Method5Impl>>;

  // call methods to make packet
  Method<03, void(int a, std::string b)> method_3;
  Method<04, PromiseView<int>(int a)> method_4;
  Method<05, SubContext<ApiLevel1>(int a)> method_5;

  // to signal method impl is called
  Event<void(int a, std::string const& b)> method_3_event;
  Event<void(RequestId request_id, int a)> method_4_event;

  ApiLevel1 api_level1;
  ProtocolContext* protocol_context;
};

void test_ApiMethodInvoke() {
  ae::tele::TeleInit::Init();

  ActionProcessor ap;
  ProtocolContext pc;

  auto api_level0 = ApiLevel0{pc, ActionContext{ap}};
  auto call_context = ApiContext{pc, api_level0};

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

  ap.Update(Now());

  TEST_ASSERT(level0_method3_called);
  TEST_ASSERT(level1_method3_called);
}

void test_ReturnResult() {
  ActionProcessor ap;
  ProtocolContext pc;

  bool promise_get_value = false;

  auto api_level0 = ApiLevel0{pc, ActionContext{ap}};
  auto call_context = ApiContext{pc, api_level0};

  auto promise = call_context->method_4(42);
  promise->ResultEvent().Subscribe([&](auto const& promise) {
    auto value = promise.value();
    TEST_ASSERT_EQUAL(78, value);
    promise_get_value = true;
  });
  promise->ErrorEvent().Subscribe([](auto const&) { TEST_FAIL(); });

  DataBuffer packet = std::move(call_context);

  AssertPacket(packet, MessageId{4}, Skip<RequestId>{}, int{42});

  bool level0_method4_called = false;

  EventSubscriber{api_level0.method_4_event}.Subscribe(
      [&](RequestId req_id, int) {
        level0_method4_called = true;
        auto response_context = ApiContext{pc, api_level0};
        response_context->SendResult(req_id, 78);
        auto data = std::move(response_context).Pack();
        auto parser = ApiParser{pc, data};
        // send/receive
        parser.Parse(api_level0);
      });

  // send/receive
  auto parser = ApiParser{pc, packet};
  parser.Parse(api_level0);

  ap.Update(Now());

  TEST_ASSERT(level0_method4_called);
  TEST_ASSERT(promise_get_value);
}

}  // namespace ae::test_method_call

int test_method_call() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_method_call::test_ApiMethodInvoke);
  RUN_TEST(ae::test_method_call::test_ReturnResult);
  return UNITY_END();
}
