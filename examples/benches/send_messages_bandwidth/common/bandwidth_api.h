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
  virtual void Handshake();
  // sender sends start or stop test and wait for receiver's response true to
  // continue/stop tests
  virtual void StartTest();
  virtual void StopTest();

  virtual void Message(std::uint16_t id, PayloadData const& data);

  virtual void HandshakeResult(bool accepted);
  virtual void StartTestResult(bool accepted);
  virtual void StopTestResult(bool accepted);

  API_LIST(METHOD(0x03, Handshake), METHOD(0x04, StartTest),
           METHOD(0x05, StopTest), METHOD(0x06, Message),
           METHOD(0x07, HandshakeResult), METHOD(0x08, StartTestResult),
           METHOD(0x09, StopTestResult))
};

class BandwidthApiServer : public BandwidthApi {
 public:
  explicit BandwidthApiServer(EventSystem& es);

  void Handshake() override;
  void StartTest() override;
  void StopTest() override;
  void Message(std::uint16_t id, PayloadData const& data) override;
  void HandshakeResult(bool accepted) override;
  void StartTestResult(bool accepted) override;
  void StopTestResult(bool accepted) override;

  Event<void()> handshake_event;
  Event<void()> start_test_event;
  Event<void()> stop_test_event;
  Event<void(std::uint16_t id, PayloadData const& data)> message_event;
  Event<void(bool accepted)> handshake_result_event;
  Event<void(bool accepted)> start_test_result_event;
  Event<void(bool accepted)> stop_test_result_event;
};

}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_BANDWIDTH_API_H_
