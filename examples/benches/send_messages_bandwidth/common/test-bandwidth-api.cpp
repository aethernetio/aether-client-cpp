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

#include <utility>
#include <vector>

#include "aether/tasks/manual_task_scheduler.h"
#include "send_messages_bandwidth/common/bandwidth_api.h"

namespace ae::test_bandwidth_api {
void test_PushAcknowledgementsAndPayloadRoundTrip() {
  EventSystem event_system;
  TaskScheduler scheduler;
  ProtocolContext protocol_context{scheduler, event_system};
  bench::BandwidthApi requests;
  bench::BandwidthApi responses;
  bench::BandwidthApiServer receiver{event_system};
  bench::BandwidthApiServer sender{event_system};
  std::vector<DataBuffer> acknowledgements;
  unsigned request_count{};
  unsigned response_count{};
  unsigned message_count{};
  MultiSubscription subscriptions;
  subscriptions += receiver.handshake_event.Subscribe([&]() {
    ++request_count;
    ApiContext context{responses};
    context->HandshakeResult(true);
    acknowledgements.push_back(std::move(context).Pack());
  });
  subscriptions += receiver.start_test_event.Subscribe([&]() {
    ++request_count;
    ApiContext context{responses};
    context->StartTestResult(false);
    acknowledgements.push_back(std::move(context).Pack());
  });
  subscriptions += receiver.stop_test_event.Subscribe([&]() {
    ++request_count;
    ApiContext context{responses};
    context->StopTestResult(true);
    acknowledgements.push_back(std::move(context).Pack());
  });
  subscriptions += receiver.message_event.Subscribe(
      [&](std::uint16_t id, DataBuffer const& payload) {
        TEST_ASSERT_EQUAL(42, id);
        TEST_ASSERT_EQUAL(3, payload.size());
        TEST_ASSERT_EQUAL(7, payload[0]);
        TEST_ASSERT_EQUAL(9, payload[2]);
        ++message_count;
      });
  subscriptions += sender.handshake_result_event.Subscribe([&](bool accepted) {
    TEST_ASSERT_TRUE(accepted);
    ++response_count;
  });
  subscriptions += sender.start_test_result_event.Subscribe([&](bool accepted) {
    TEST_ASSERT_FALSE(accepted);
    ++response_count;
  });
  subscriptions += sender.stop_test_result_event.Subscribe([&](bool accepted) {
    TEST_ASSERT_TRUE(accepted);
    ++response_count;
  });

  ApiContext context{requests};
  context->Handshake();
  context->StartTest();
  context->StopTest();
  auto packet = std::move(context).Pack();
  ApiParser parser{protocol_context, packet};
  TEST_ASSERT_TRUE(parser.Parse(receiver));
  TEST_ASSERT_EQUAL(3, request_count);
  ApiContext payload_context{requests};
  payload_context->Message(42, DataBuffer{7, 8, 9});
  auto payload_packet = std::move(payload_context).Pack();
  ApiParser payload_parser{protocol_context, payload_packet};
  TEST_ASSERT_TRUE(payload_parser.Parse(receiver));
  TEST_ASSERT_EQUAL(1, message_count);
  TEST_ASSERT_EQUAL(3, acknowledgements.size());
  for (auto const& acknowledgement : acknowledgements) {
    ApiParser result_parser{protocol_context, acknowledgement};
    TEST_ASSERT_TRUE(result_parser.Parse(sender));
  }
  TEST_ASSERT_EQUAL(3, response_count);
}
}  // namespace ae::test_bandwidth_api

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_bandwidth_api::test_PushAcknowledgementsAndPayloadRoundTrip);
  return UNITY_END();
}
