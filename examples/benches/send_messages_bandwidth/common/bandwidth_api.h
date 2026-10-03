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

#include "aether/api_protocol/api_protocol.h"
#include "aether/events/events.h"
#include "aether/types/data_buffer.h"

namespace ae::bench {
class BandwidthApi : public DeclareApi<BandwidthApi> {
 public:
  using PayloadData = DataBuffer;

  virtual ~BandwidthApi() = default;

  // sender sends handshake until receiver doesn't answers true
  virtual ApiPromise<bool> Handshake();
  // sender sends start or stop test and wait for receiver's response true to
  // continue/stop tests
  virtual ApiPromise<bool> StartTest();
  virtual ApiPromise<bool> StopTest();

  virtual void Message(std::uint16_t id, PayloadData const& data);

  API_LIST(METHOD(0x03, Handshake), METHOD(0x04, StartTest),
           METHOD(0x05, StopTest), METHOD(0x06, Message))
};

class BandwidthApiServer : public BandwidthApi {
 public:
  explicit BandwidthApiServer(EventSystem& es);

  ApiPromise<bool> Handshake() override;
  ApiPromise<bool> StartTest() override;
  ApiPromise<bool> StopTest() override;
  void Message(std::uint16_t id, PayloadData const& data) override;

  Event<void(RequestId req_id)> handshake_event;
  Event<void(RequestId req_id)> start_test_event;
  Event<void(RequestId req_id)> stop_test_event;
  Event<void(std::uint16_t id, PayloadData const& data)> message_event;
};

}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
