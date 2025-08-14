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

#include "send_messages_bandwidth/common/bandwidth_api.h"

namespace ae::bench {
BandwidthApi::BandwidthApi(ActionContext action_context,
                           ProtocolContext& protocol_context)
    : ReturnResultApiImpl{protocol_context},
      handshake{protocol_context, action_context},
      start_test{protocol_context, action_context},
      stop_test{protocol_context, action_context},
      message{protocol_context} {}

void BandwidthApi::HandshakeImpl(ApiParser&, PromiseResult<bool> res) {
  handshake_event_.Emit(res.request_id);
}

void BandwidthApi::StartTestImpl(ApiParser&, PromiseResult<bool> res) {
  start_test_event_.Emit(res.request_id);
}

void BandwidthApi::StopTestImpl(ApiParser&, PromiseResult<bool> res) {
  stop_test_event_.Emit(res.request_id);
}

void BandwidthApi::MessageImpl(ApiParser&, std::uint16_t id, PayloadData data) {
  message_event_.Emit(id, std::move(data));
}

EventSubscriber<void(RequestId req_id)> BandwidthApi::handshake_event() {
  return EventSubscriber{handshake_event_};
}

EventSubscriber<void(RequestId req_id)> BandwidthApi::start_test_event() {
  return EventSubscriber{start_test_event_};
}

EventSubscriber<void(RequestId req_id)> BandwidthApi::stop_test_event() {
  return EventSubscriber{stop_test_event_};
}

EventSubscriber<void(std::uint16_t id, BandwidthApi::PayloadData&& data)>
BandwidthApi::message_event() {
  return EventSubscriber{message_event_};
}
}  // namespace ae::bench
