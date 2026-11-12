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
void BandwidthApi::Handshake() { ClientMethod<&BandwidthApi::Handshake>(); }
void BandwidthApi::StartTest() { ClientMethod<&BandwidthApi::StartTest>(); }
void BandwidthApi::StopTest() { ClientMethod<&BandwidthApi::StopTest>(); }
void BandwidthApi::Message(std::uint16_t id, PayloadData const& data) {
  ClientMethod<&BandwidthApi::Message>(id, data);
}
void BandwidthApi::HandshakeResult(bool accepted) {
  ClientMethod<&BandwidthApi::HandshakeResult>(accepted);
}
void BandwidthApi::StartTestResult(bool accepted) {
  ClientMethod<&BandwidthApi::StartTestResult>(accepted);
}
void BandwidthApi::StopTestResult(bool accepted) {
  ClientMethod<&BandwidthApi::StopTestResult>(accepted);
}

BandwidthApiServer::BandwidthApiServer(EventSystem& es)
    : handshake_event{es},
      start_test_event{es},
      stop_test_event{es},
      message_event{es},
      handshake_result_event{es},
      start_test_result_event{es},
      stop_test_result_event{es} {}

void BandwidthApiServer::Handshake() { handshake_event.Emit(); }
void BandwidthApiServer::StartTest() { start_test_event.Emit(); }
void BandwidthApiServer::StopTest() { stop_test_event.Emit(); }
void BandwidthApiServer::Message(std::uint16_t id, PayloadData const& data) {
  message_event.Emit(id, data);
}
void BandwidthApiServer::HandshakeResult(bool accepted) {
  handshake_result_event.Emit(accepted);
}
void BandwidthApiServer::StartTestResult(bool accepted) {
  start_test_result_event.Emit(accepted);
}
void BandwidthApiServer::StopTestResult(bool accepted) {
  stop_test_result_event.Emit(accepted);
}

}  // namespace ae::bench
