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

#include <cstdint>

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/work_cloud_api/work_server_api/authorized_api.h"
#include "assert_packet.h"

namespace ae::test_api_protocol_request_response {

class ResponseApi : public DeclareApi<ResponseApi> {
 public:
  virtual ~ResponseApi() = default;

  virtual ApiPromise<std::int32_t> Request() = 0;

  API_LIST(METHOD(2, Request))
};

class ClientResponseApi final : public ResponseApi {
 public:
  ApiPromise<std::int32_t> Request() override {
    return ClientMethod<&ResponseApi::Request>();
  }
};

class ServerResponseApi final : public ResponseApi {
 public:
  explicit ServerResponseApi(EventSystem& event_system)
      : request{event_system} {}

  ApiPromise<std::int32_t> Request() override {
    request.Emit(server_context().request_id());
    return {};
  }

  Event<void(RequestId)> request;
};

struct RequestResponseFixture {
  EventSystem event_system;
  TaskScheduler scheduler;
  ProtocolContext protocol_context{scheduler, event_system};
};

void test_SendErrorWireFormat() {
  EventSystem event_system;
  auto api = ServerResponseApi{event_system};
  auto context = ApiContext{api};
  context->SendError(RequestId{42}, 7, -9);

  DataBuffer packet = std::move(context);
  AssertPacket(packet, MessageId{1}, RequestId{42}, std::uint8_t{7},
               std::int32_t{-9});
}

void test_ErrorResponseCompletesPromiseOnce() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{};
  auto client_context = ApiContext{client_api, fxtr.protocol_context};

  auto promise = client_context->Request();
  auto completion_count = 0;
  auto error_code = std::int32_t{};
  Subscription subscription = promise.Subscribe([&](auto const& result) {
    TEST_ASSERT_FALSE(result.IsOk());
    error_code = static_cast<std::int32_t>(result.error());
    ++completion_count;
  });
  (void)subscription;

  DataBuffer request = std::move(client_context);
  auto server_api = ServerResponseApi{fxtr.event_system};
  DataBuffer response;
  Subscription response_subscription =
      server_api.request.Subscribe([&](RequestId request_id) {
        auto response_context = ApiContext{server_api};
        response_context->SendError(request_id, 3, -42);
        response = std::move(response_context);

        auto response_parser = ApiParser{fxtr.protocol_context, response};
        TEST_ASSERT_TRUE(response_parser.Parse(server_api));
      });
  (void)response_subscription;

  auto request_parser = ApiParser{fxtr.protocol_context, request};
  TEST_ASSERT_TRUE(request_parser.Parse(server_api));
  TEST_ASSERT_EQUAL(1, completion_count);
  TEST_ASSERT_EQUAL(-42, error_code);

  auto duplicate_response_parser = ApiParser{fxtr.protocol_context, response};
  TEST_ASSERT_FALSE(duplicate_response_parser.Parse(server_api));
  TEST_ASSERT_EQUAL(1, completion_count);
}

void test_TruncatedResultCompletesPromiseWithDecodeError() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{};
  auto client_context = ApiContext{client_api, fxtr.protocol_context};
  auto promise = client_context->Request();
  auto completion_count = 0;
  auto error_code = std::int32_t{};
  Subscription subscription = promise.Subscribe([&](auto const& result) {
    TEST_ASSERT_FALSE(result.IsOk());
    error_code = static_cast<std::int32_t>(result.error());
    ++completion_count;
  });
  (void)subscription;

  auto server_api = ServerResponseApi{fxtr.event_system};
  auto response_context = ApiContext{server_api};
  response_context->SendResult(promise.request_id(), std::int32_t{42});
  DataBuffer response = std::move(response_context);
  response.pop_back();

  auto parser = ApiParser{fxtr.protocol_context, response};
  TEST_ASSERT_FALSE(parser.Parse(server_api));
  TEST_ASSERT_EQUAL(1, completion_count);
  TEST_ASSERT_EQUAL(-2, error_code);
}

void test_TruncatedResponseRequestIdDoesNotCompletePromise() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{};
  auto client_context = ApiContext{client_api, fxtr.protocol_context};
  auto promise = client_context->Request();
  auto completion_count = 0;
  Subscription subscription =
      promise.Subscribe([&](auto const&) { ++completion_count; });
  (void)subscription;

  auto server_api = ServerResponseApi{fxtr.event_system};
  auto malformed_packet = DataBuffer{0};
  auto parser = ApiParser{fxtr.protocol_context, malformed_packet};

  TEST_ASSERT_FALSE(parser.Parse(server_api));
  TEST_ASSERT_EQUAL(0, completion_count);
}

void test_TruncatedErrorDoesNotCompletePromise() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{};
  auto client_context = ApiContext{client_api, fxtr.protocol_context};
  auto promise = client_context->Request();
  auto completion_count = 0;
  Subscription subscription =
      promise.Subscribe([&](auto const&) { ++completion_count; });
  (void)subscription;

  auto server_api = ServerResponseApi{fxtr.event_system};
  auto response_context = ApiContext{server_api};
  response_context->SendError(promise.request_id(), 3, -42);
  DataBuffer response = std::move(response_context);
  response.pop_back();

  auto parser = ApiParser{fxtr.protocol_context, response};
  TEST_ASSERT_FALSE(parser.Parse(server_api));
  TEST_ASSERT_EQUAL(0, completion_count);
}

void test_PushPingAndTimingRpcShareProtocol() {
  RequestResponseFixture fxtr;
  AuthorizedApi api;
  auto push = ApiContext{api, fxtr.protocol_context};
  push->Ping(1000, 1000);
  DataBuffer ping_packet = std::move(push);
  AssertPacket(ping_packet, MessageId{4}, std::uint64_t{1000},
               std::uint64_t{1000});

  auto rpc = ApiContext{api, fxtr.protocol_context};
  auto uid = Uid{};
  auto promise = rpc->GetClientTiming(uid);
  TEST_ASSERT_EQUAL(0, promise.request_id().id);
  auto count = 0;
  Subscription sub = promise.Subscribe([&](auto const& result) {
    TEST_ASSERT_TRUE(result.IsOk());
    TEST_ASSERT_EQUAL(-1500, result.value().next_ping_delta_ms);
    TEST_ASSERT_EQUAL(42, result.value().last_connect_delta_ms);
    ++count;
  });
  DataBuffer request = std::move(rpc);
  AssertPacket(request, MessageId{35}, promise.request_id(), uid);
  auto reply = ApiContext{api};
  reply->SendResult(promise.request_id(), ClientTiming{-1500, 42});
  DataBuffer response = std::move(reply);
  AssertPacket(response, MessageId{0}, promise.request_id(), std::int64_t{42},
               std::int64_t{-1500});
  auto parser = ApiParser{fxtr.protocol_context, response};
  ClientResponseApi receiver;
  TEST_ASSERT_TRUE(parser.Parse(receiver));
  TEST_ASSERT_EQUAL(1, count);
}

}  // namespace ae::test_api_protocol_request_response

int test_api_protocol_request_response() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_request_response;  // NOLINT

  RUN_TEST(test_PushPingAndTimingRpcShareProtocol);
  RUN_TEST(test_SendErrorWireFormat);
  RUN_TEST(test_ErrorResponseCompletesPromiseOnce);
  RUN_TEST(test_TruncatedResultCompletesPromiseWithDecodeError);
  RUN_TEST(test_TruncatedResponseRequestIdDoesNotCompletePromise);
  RUN_TEST(test_TruncatedErrorDoesNotCompletePromise);
  return UNITY_END();
}
