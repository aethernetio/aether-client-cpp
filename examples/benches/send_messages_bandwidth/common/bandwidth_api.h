/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_

#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/return_result_api.h"

namespace ae::bench {
class BandwidthApi : public ApiClassImpl<BandwidthApi> {
 public:
  using PayloadData = DataBuffer;

  BandwidthApi(ActionContext action_context, ProtocolContext& protocol_context);

  // sender sends handshake until receiver doesn't answers true
  Method<0x03, ApiPromisePtr<bool>()> handshake;
  // sender sends start or stop test and wait for receiver's response true to
  // continue/stop tests
  Method<0x04, ApiPromisePtr<bool>()> start_test;
  Method<0x05, ApiPromisePtr<bool>()> stop_test;

  Method<0x06, void(std::uint16_t id, PayloadData data)> message;

  void HandshakeImpl(ApiParser& parser, PromiseResult<bool> res);
  void StartTestImpl(ApiParser& parser, PromiseResult<bool> res);
  void StopTestImpl(ApiParser& parser, PromiseResult<bool> res);
  void MessageImpl(ApiParser& parser, std::uint16_t id, PayloadData data);

  ReturnResultApi return_result;

  using ApiMethods = ImplList<RegMethod<0x03, &BandwidthApi::HandshakeImpl>,
                              RegMethod<0x04, &BandwidthApi::StartTestImpl>,
                              RegMethod<0x05, &BandwidthApi::StopTestImpl>,
                              RegMethod<0x06, &BandwidthApi::MessageImpl>,
                              ExtApi<&BandwidthApi::return_result>>;

  EventSubscriber<void(RequestId req_id)> handshake_event();
  EventSubscriber<void(RequestId req_id)> start_test_event();
  EventSubscriber<void(RequestId req_id)> stop_test_event();
  EventSubscriber<void(std::uint16_t id, PayloadData&& data)> message_event();

 private:
  Event<void(RequestId req_id)> handshake_event_;
  Event<void(RequestId req_id)> start_test_event_;
  Event<void(RequestId req_id)> stop_test_event_;
  Event<void(std::uint16_t id, PayloadData&& data)> message_event_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
