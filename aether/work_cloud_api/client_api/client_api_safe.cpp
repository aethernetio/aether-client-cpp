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

#include "aether/work_cloud_api/client_api/client_api_safe.h"

#include <cassert>

#include "aether/api_protocol/api_protocol.h"

#include "aether/tele/tele.h"

namespace ae {
ClientApiSafe::ClientApiSafe(ProtocolContext& protocol_context)
    : ApiClassImpl{protocol_context}, return_result{protocol_context} {}

void ClientApiSafe::SendMessages(ApiParser& /* parser */,
                                 std::vector<AeMessage> messages) {
  for (auto const& msg : messages) {
    AE_TELED_DEBUG("Received message uid:{}", msg.uid);
    send_message_event_.Emit(msg);
  }
}

void ClientApiSafe::SendServerDescriptor(ApiParser& /* parser */,
                                         ServerDescriptor server_descriptor) {
  send_server_descriptor_event_.Emit(server_descriptor);
}

void ClientApiSafe::SendServerDescriptors(
    ApiParser& /* parser */, std::vector<ServerDescriptor> server_descriptors) {
  for (auto const& sd : server_descriptors) {
    send_server_descriptor_event_.Emit(sd);
  }
}

void ClientApiSafe::SendCloud(ApiParser& /* parser */, Uid uid,
                              CloudDescriptor cloud) {
  send_cloud_event_.Emit(uid, cloud);
}

void ClientApiSafe::SendClouds(ApiParser& /* parser */,
                               std::vector<UidAndCloudDescriptor> clouds) {
  for (auto const& cloud : clouds) {
    send_cloud_event_.Emit(cloud.uid, cloud.cloud);
  }
}

void ClientApiSafe::RequestTelemetry(ApiParser& /* parser */) {
  request_telemetry_event_.Emit();
}

}  // namespace ae
