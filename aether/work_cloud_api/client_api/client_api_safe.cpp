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

void ClientApiSafe::ChangeParent([[maybe_unused]] Uid const& uid) {
  AE_TELED_DEBUG("ChangeParent");
}

void ClientApiSafe::ChangeAlias([[maybe_unused]] Uid const& uid) {
  AE_TELED_DEBUG("ChangeAlias");
}

void ClientApiSafe::NewChildren([[maybe_unused]] std::vector<Uid> const& uids) {
  AE_TELED_DEBUG("NewChildren");
}

void ClientApiSafe::SendMessages(std::vector<AeMessage> const& messages) {
  for (auto const& msg : messages) {
    AE_TELED_DEBUG("Received message uid:{}", msg.uid);
    send_message_event_.Emit(msg);
  }
}

void ClientApiSafe::SendServerDescriptor(
    ServerDescriptor const& server_descriptor) {
  send_server_descriptor_event_.Emit(server_descriptor);
}

void ClientApiSafe::SendServerDescriptors(
    std::vector<ServerDescriptor> const& server_descriptors) {
  for (auto const& sd : server_descriptors) {
    send_server_descriptor_event_.Emit(sd);
  }
}

void ClientApiSafe::SendCloud(Uid const& uid, CloudDescriptor const& cloud) {
  send_cloud_event_.Emit(uid, cloud);
}

void ClientApiSafe::SendClouds(
    std::vector<UidAndCloudDescriptor> const& clouds) {
  for (auto const& cloud : clouds) {
    send_cloud_event_.Emit(cloud.uid, cloud.cloud);
  }
}

void ClientApiSafe::RequestTelemetry() { request_telemetry_event_.Emit(); }

void ClientApiSafe::SendAccessGroups(
    [[maybe_unused]] std::vector<AccessGroup> const& access_group) {
  AE_TELED_DEBUG("SendAccessGroups");
}
void ClientApiSafe::SendAccessGroupForClient(
    [[maybe_unused]] Uid const& uid,
    [[maybe_unused]] std::vector<std::uint64_t> const& groups) {
  AE_TELED_DEBUG("SendAccessGroupForClient");
}
void ClientApiSafe::AddItemsToAccessGroup(
    [[maybe_unused]] std::uint64_t id,
    [[maybe_unused]] std::vector<Uid> const& groups) {
  AE_TELED_DEBUG("AddItemsToAccessGroup");
}
void ClientApiSafe::RemoveItemsFromAccessGroup(
    [[maybe_unused]] std::uint64_t id,
    [[maybe_unused]] std::vector<Uid> const& groups) {
  AE_TELED_DEBUG("RemoveItemsFromAccessGroup");
}
void ClientApiSafe::AddAccessGroupsToClient(
    [[maybe_unused]] Uid const& uid,
    [[maybe_unused]] std::vector<std::uint64_t> const& groups) {
  AE_TELED_DEBUG("AddAccessGroupsToClient");
}
void ClientApiSafe::RemoveAccessGroupsFromClient(
    [[maybe_unused]] Uid const& uid,
    [[maybe_unused]] std::vector<std::uint64_t> const& groups) {
  AE_TELED_DEBUG("RemoveAccessGroupsFromClient");
}
void ClientApiSafe::SendAllAccessedClients(
    [[maybe_unused]] Uid const& uid,
    [[maybe_unused]] std::vector<Uid> const& accessed_clients) {
  AE_TELED_DEBUG("SendAllAccessedClients");
}
void ClientApiSafe::SendAccessCheckResults(
    [[maybe_unused]] std::vector<AccessCheckResult> const& results) {
  AE_TELED_DEBUG("SendAccessCheckResults");
}

void ClientApiSafe::SendMessage(AeMessage const& msg) {
  AE_TELED_DEBUG("Received message uid:{}", msg.uid);
  send_message_event_.Emit(msg);
}

void ClientApiSafe::SendCLoudConfig(std::vector<CloudConfig> const& configs) {
  AE_TELED_DEBUG("Received cloud configs count {}", configs.size());
  send_cloud_configs_.Emit(configs);
}

}  // namespace ae
