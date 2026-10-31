
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

#include "assert_packet.h"

namespace ae::test_api_protocol_request_response {

struct IntMessage {
  std::int32_t value;
  AE_REFLECT_MEMBERS(value)
};


class ResponseApi : public DeclareApi<ResponseApi> {
 public:
  virtual ~ResponseApi() = default;

  virtual void Request() = 0;
  virtual void OnResponse(std::int32_t value) = 0;

  API_LIST(METHOD(2, Request), METHOD(3, OnResponse))
};

class ClientResponseApi final : public ResponseApi {
 public:
  explicit ClientResponseApi(EventSystem& event_system)
      : response{event_system} {}



  void Request() override { ClientMethod<&ResponseApi::Request>(); }

  void OnResponse(std::int32_t value) override { response.Emit(value); }

  Event<void(std::int32_t)> response;
};

class ServerResponseApi final : public ResponseApi {
 public:
  explicit ServerResponseApi(EventSystem& event_system)
      : request{event_system} {}

  void Request() override { request.Emit(); }

  void OnResponse(std::int32_t) override {}

  Event<void()> request;
};

struct RequestResponseFixture {
  EventSystem event_system;
  TaskScheduler scheduler;
  ProtocolContext protocol_context{scheduler, event_system};
};

void test_RequestSendsWithoutRequestId() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{fxtr.event_system};
  auto client_context = ApiContext{client_api, fxtr.protocol_context};

  client_context->Request();

  DataBuffer request = std::move(client_context);
  AssertPacket(request, MessageId{2});
}

void test_PushResponseIsDeliveredToClientEvent() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{fxtr.event_system};

  auto response_value = std::int32_t{0};
  auto response_received = false;
  Subscription subscription = client_api.response.Subscribe([&](std::int32_t v) {
    response_value = v;
    response_received = true;
  });

  DataBuffer response;
  ApiPacker packer{response};
  packer.Pack(MessageId{3}, IntMessage{78});

  auto parser = ApiParser{fxtr.protocol_context, response};
  TEST_ASSERT_TRUE(parser.Parse(client_api));
  TEST_ASSERT_TRUE(response_received);
  TEST_ASSERT_EQUAL(78, response_value);
}

void test_TruncatedPushResponseIsRejected() {
  RequestResponseFixture fxtr;
  auto client_api = ClientResponseApi{fxtr.event_system};

  DataBuffer malformed_packet{0xFF, 0xFF};
  auto parser = ApiParser{fxtr.protocol_context, malformed_packet};
  TEST_ASSERT_FALSE(parser.Parse(client_api));
}

// TODO(push-model): The old request/response tests (SendError wire format,
// promise completion, etc.) are removed because request_id and ApiPromise
// no longer exist in the push model.

}  // namespace ae::test_api_protocol_request_response

int test_api_protocol_request_response() {
  UNITY_BEGIN();
  using namespace ae::test_api_protocol_request_response;  // NOLINT

  RUN_TEST(test_RequestSendsWithoutRequestId);
  RUN_TEST(test_PushResponseIsDeliveredToClientEvent);
  RUN_TEST(test_TruncatedPushResponseIsRejected);
  return UNITY_END();
}