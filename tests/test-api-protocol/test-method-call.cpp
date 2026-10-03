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

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/types/data_buffer.h"

#include "assert_packet.h"

namespace ae::test_api_protocol_method_call {

struct Number {
  AE_REFLECT_MEMBERS(value)
  int value;
};

class ApiLevel1 : public ae::DeclareApi<ApiLevel1> {
 public:
  virtual ~ApiLevel1() = default;

  virtual void Method3(float a) = 0;

  API_LIST(METHOD(3, Method3))
};

class ApiLevel0 : public ae::DeclareApi<ApiLevel0> {
 public:
  virtual ~ApiLevel0() = default;

  virtual void Method3(int a, std::string const& b) = 0;
  virtual ae::ApiPromise<Number> Method4(int a) = 0;
  virtual void Method6(int a, ae::SubApi<ApiLevel1> sub_api) = 0;

  API_LIST(METHOD(3, Method3), METHOD(4, Method4), METHOD(6, Method6))
};

namespace client {
class ApiLevel1 : public test_api_protocol_method_call::ApiLevel1 {
 public:
  void Method3(float a) override {
    ClientMethod<&test_api_protocol_method_call::ApiLevel1::Method3>(a);
  }
};

class ApiLevel0 : public test_api_protocol_method_call::ApiLevel0 {
 public:
  void Method3(int a, std::string const& b) override {
    ClientMethod<&test_api_protocol_method_call::ApiLevel0::Method3>(a, b);
  }
  ae::ApiPromise<Number> Method4(int a) override {
    return ClientMethod<&test_api_protocol_method_call::ApiLevel0::Method4>(a);
  }
  void Method6(
      int a,
      ae::SubApi<test_api_protocol_method_call::ApiLevel1> sub_api) override {
    ClientMethod<&test_api_protocol_method_call::ApiLevel0::Method6>(
        a, std::move(sub_api)(level1, client_context()));
  }

  ApiLevel1 level1;
};
}  // namespace client

namespace server {
class ApiLevel1 : public test_api_protocol_method_call::ApiLevel1 {
 public:
  explicit ApiLevel1(ae::EventSystem& es) : on_method3{es} {}

  void Method3(float a) override { on_method3.Emit(a); }

  ae::Event<void(float)> on_method3;
};

class ApiLevel0 : public test_api_protocol_method_call::ApiLevel0 {
 public:
  explicit ApiLevel0(ae::EventSystem& es)
      : on_method3{es}, on_method4{es}, on_method6{es}, level1{es} {}

  void Method3(int a, std::string const& b) override { on_method3.Emit(a, b); }
  ae::ApiPromise<Number> Method4(int a) override {
    on_method4.Emit(a, server_context().request_id());
    return {};
  }
  void Method6(
      int a,
      ae::SubApi<test_api_protocol_method_call::ApiLevel1> sub_api) override {
    on_method6.Emit(a);
    auto buff = std::move(sub_api).buffer();
    auto parser = ae::ApiParser{server_context().protocol_context(), buff};
    parser.Parse(level1);
  }

  ae::Event<void(int a, std::string const& b)> on_method3;
  ae::Event<void(int a, ae::RequestId req_id)> on_method4;
  ae::Event<void(int a)> on_method6;
  ApiLevel1 level1;
};

}  // namespace server

struct MethodCallFixture {
  ae::EventSystem event_system;
  ae::TaskScheduler scheduler;
  ae::ProtocolContext protocol_context{scheduler, event_system};
};

void test_ReturnResult() {
  MethodCallFixture fxtr;

  bool promise_get_value = false;

  auto client_level0 = client::ApiLevel0{};
  auto call_context = ae::ApiContext{client_level0, fxtr.protocol_context};

  auto promise = call_context->Method4(42);
  promise.Subscribe([&](auto const& res) {
    if (res.IsOk()) {
      auto value = res.value().value;
      TEST_ASSERT_EQUAL(78, value);
      promise_get_value = true;
    } else {
      TEST_FAIL();
    }
  });

  ae::DataBuffer packet = std::move(call_context);

  AssertPacket(packet, ae::MessageId{4}, Skip<ae::RequestId>{}, int{42});

  bool level0_method4_called = false;

  auto server_level0 = server::ApiLevel0{fxtr.event_system};

  server_level0.on_method4.Subscribe([&](int, ae::RequestId req_id) {
    level0_method4_called = true;
    // send response to req_id
    auto response_context = ae::ApiContext{server_level0};
    response_context->SendResult(req_id, Number{78});
    auto data = std::move(response_context).Pack();
    // simulate transport and parse response here
    // response should be parsed by server implementation on sender side, but in
    // our case more matter to use same protocol_context
    auto parser = ae::ApiParser{fxtr.protocol_context, data};
    parser.Parse(server_level0);
  });

  // simulate transport and parse sent packet
  auto parser = ae::ApiParser{fxtr.protocol_context, packet};
  parser.Parse(server_level0);

  TEST_ASSERT(level0_method4_called);
  TEST_ASSERT(promise_get_value);
}

void test_MethodWithSubApi() {
  MethodCallFixture fxtr;

  auto client_level0 = client::ApiLevel0{};
  auto call_context = ae::ApiContext{client_level0, fxtr.protocol_context};

  call_context->Method6(
      12, [&](ae::ApiContext<ApiLevel1>& api) { api->Method3(42.12F); });

  ae::DataBuffer packet = std::move(call_context);
  AssertPacket(packet, ae::MessageId{6}, int{12}, Skip<ae::PackedSize>{},
               ae::MessageId{3}, float{42.12F});

  bool level0_method6_called = false;
  bool level1_method3_called = false;

  auto server_level0 = server::ApiLevel0{fxtr.event_system};

  server_level0.on_method6.Subscribe(
      [&](int) { level0_method6_called = true; });

  server_level0.level1.on_method3.Subscribe(
      [&](float) { level1_method3_called = true; });

  auto parser = ae::ApiParser{fxtr.protocol_context, packet};
  parser.Parse(server_level0);

  TEST_ASSERT_TRUE(level0_method6_called);
  TEST_ASSERT_TRUE(level1_method3_called);
}

void test_TruncatedArgumentsDoNotInvokeMethod() {
  MethodCallFixture fxtr;
  auto server_level0 = server::ApiLevel0{fxtr.event_system};
  auto method_called = false;
  Subscription subscription = server_level0.on_method3.Subscribe(
      [&](int, std::string const&) { method_called = true; });
  (void)subscription;

  auto malformed_packet = DataBuffer{3};
  auto parser = ApiParser{fxtr.protocol_context, malformed_packet};

  TEST_ASSERT_FALSE(parser.Parse(server_level0));
  TEST_ASSERT_FALSE(method_called);
}

void test_TruncatedRequestIdDoesNotInvokeMethod() {
  MethodCallFixture fxtr;
  auto server_level0 = server::ApiLevel0{fxtr.event_system};
  auto method_called = false;
  Subscription subscription = server_level0.on_method4.Subscribe(
      [&](int, RequestId) { method_called = true; });
  (void)subscription;

  auto malformed_packet = DataBuffer{4};
  auto parser = ApiParser{fxtr.protocol_context, malformed_packet};

  TEST_ASSERT_FALSE(parser.Parse(server_level0));
  TEST_ASSERT_FALSE(method_called);
}

}  // namespace ae::test_api_protocol_method_call

int test_api_protocol_method_call() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_method_call;  // NOLINT

  RUN_TEST(test_ReturnResult);
  RUN_TEST(test_MethodWithSubApi);
  RUN_TEST(test_TruncatedArgumentsDoNotInvokeMethod);
  RUN_TEST(test_TruncatedRequestIdDoesNotInvokeMethod);
  return UNITY_END();
}
