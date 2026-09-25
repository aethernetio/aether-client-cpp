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
ApiPromise<bool> BandwidthApi::Handshake() {
  return ClientMethod<&BandwidthApi::Handshake>();
}
ApiPromise<bool> BandwidthApi::StartTest() {
  return ClientMethod<&BandwidthApi::StartTest>();
}
ApiPromise<bool> BandwidthApi::StopTest() {
  return ClientMethod<&BandwidthApi::StopTest>();
}
void BandwidthApi::Message(std::uint16_t id, PayloadData const& data) {
  ClientMethod<&BandwidthApi::Message>(id, data);
}

BandwidthApiServer::BandwidthApiServer(EventSystem& es)
    : handshake_event{es},
      start_test_event{es},
      stop_test_event{es},
      message_event{es} {}

ApiPromise<bool> BandwidthApiServer::Handshake() {
  handshake_event.Emit(server_context().request_id());
  return {};
}
ApiPromise<bool> BandwidthApiServer::StartTest() {
  start_test_event.Emit(server_context().request_id());
  return {};
}
ApiPromise<bool> BandwidthApiServer::StopTest() {
  stop_test_event.Emit(server_context().request_id());
  return {};
}
void BandwidthApiServer::Message(std::uint16_t id, PayloadData const& data) {
  message_event.Emit(id, data);
}

}  // namespace ae::bench
