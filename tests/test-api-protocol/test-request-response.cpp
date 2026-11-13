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

#include "aether/work_cloud_api/client_api/client_api_safe.h"
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

void test_PushPingAndPublishedTimingWire() {
  RequestResponseFixture fxtr;
  AuthorizedApi api;
  auto push = ApiContext{api, fxtr.protocol_context};
  push->Ping(1000, 1000);
  DataBuffer ping_packet = std::move(push);
  AssertPacket(ping_packet, MessageId{4}, std::uint64_t{1000},
               std::uint64_t{1000});

  auto timing_call = ApiContext{api, fxtr.protocol_context};
  auto const uid = Uid::FromString("00112233-4455-6677-8899-aabbccddeeff");
  timing_call->GetClientTiming(uid);
  DataBuffer request = std::move(timing_call);
  // Independent byte fixtures from the published ClientServerApi ADSL:
  // command 35 + UUID, without an RPC request ID.
  DataBuffer const expected_request{0x23, 0x00, 0x11, 0x22, 0x33, 0x44,
                                    0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
                                    0xbb, 0xcc, 0xdd, 0xee, 0xff};
  TEST_ASSERT_EQUAL(expected_request.size(), request.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_request.data(), request.data(),
                                expected_request.size());

  ClientApiSafe receiver{fxtr.event_system};
  auto count = 0;
  Subscription sub = receiver.client_timing_event().Subscribe(
      [&](Uid const& received_uid, ClientTiming const& timing) {
        TEST_ASSERT_TRUE(uid == received_uid);
        TEST_ASSERT_EQUAL(-1500, timing.next_ping_delta_ms);
        TEST_ASSERT_EQUAL(42, timing.last_connect_delta_ms);
        ++count;
      });
  // command 41 + UUID + lastActivityDeltaMs(42) + nextPingDeltaMs(-1500).
  DataBuffer const response{
      0x29, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
      0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x2a, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x24, 0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  auto parser = ApiParser{fxtr.protocol_context, response};
  TEST_ASSERT_TRUE(parser.Parse(receiver));
  TEST_ASSERT_EQUAL(1, count);
}

}  // namespace ae::test_api_protocol_request_response

int test_api_protocol_request_response() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_request_response;  // NOLINT

  RUN_TEST(test_PushPingAndPublishedTimingWire);
  RUN_TEST(test_SendErrorWireFormat);
  RUN_TEST(test_ErrorResponseCompletesPromiseOnce);
  RUN_TEST(test_TruncatedResultCompletesPromiseWithDecodeError);
  RUN_TEST(test_TruncatedResponseRequestIdDoesNotCompletePromise);
  RUN_TEST(test_TruncatedErrorDoesNotCompletePromise);
  return UNITY_END();
}
