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

#ifndef AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_SAFE_H_
#define AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_SAFE_H_

#include "aether/types/uid.h"

#include "aether/events/events.h"
#include "aether/api_protocol/api_protocol.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/uid_and_cloud.h"
#include "aether/work_cloud_api/server_descriptor.h"

namespace ae {
struct AccessGroup {
  AE_REFLECT_MEMBERS(owner, id, data);
  Uid owner;
  std::uint64_t id;
  DataBuffer data;
};

struct AccessCheckResult {
  AE_REFLECT_MEMBERS(source_uid, target_uid, has_access);
  Uid source_uid;
  Uid target_uid;
  bool has_access;
};

class ClientApiSafe : public ApiClassImpl<ClientApiSafe> {
 public:
  explicit ClientApiSafe(ProtocolContext& protocol_context);

  void ChangeParent(Uid const& uid);
  void ChangeAlias(Uid const& uid);
  void NewChildren(std::vector<Uid> const& uids);
  void SendMessages(std::vector<AeMessage> const& messages);

  void SendServerDescriptor(ServerDescriptor const& server_descriptor);
  void SendServerDescriptors(
      std::vector<ServerDescriptor> const& server_descriptors);
  void SendCloud(Uid const& uid, CloudDescriptor const& cloud);
  void SendClouds(std::vector<UidAndCloudDescriptor> const& clouds);

  void RequestTelemetry();

  void SendAccessGroups(std::vector<AccessGroup> const& access_group);
  void SendAccessGroupForClient(Uid const& uid,
                                std::vector<std::uint64_t> const& groups);
  void AddItemsToAccessGroup(std::uint64_t id, std::vector<Uid> const& groups);
  void RemoveItemsFromAccessGroup(std::uint64_t id,
                                  std::vector<Uid> const& groups);
  void AddAccessGroupsToClient(Uid const& uid,
                               std::vector<std::uint64_t> const& groups);
  void RemoveAccessGroupsFromClient(Uid const& uid,
                                    std::vector<std::uint64_t> const& groups);
  void SendAllAccessedClients(Uid const& uid,
                              std::vector<Uid> const& accessed_clients);
  void SendAccessCheckResults(std::vector<AccessCheckResult> const& results);
  void SendMessage(AeMessage const& message);

  ReturnResultApi return_result;

  AE_METHODS(RegMethod<3, &ClientApiSafe::ChangeParent>,
             RegMethod<4, &ClientApiSafe::ChangeAlias>,
             RegMethod<5, &ClientApiSafe::NewChildren>,
             RegMethod<6, &ClientApiSafe::SendMessages>,
             RegMethod<7, &ClientApiSafe::SendServerDescriptor>,
             RegMethod<8, &ClientApiSafe::SendServerDescriptors>,
             RegMethod<9, &ClientApiSafe::SendCloud>,
             RegMethod<10, &ClientApiSafe::SendClouds>,
             RegMethod<11, &ClientApiSafe::RequestTelemetry>,
             RegMethod<12, &ClientApiSafe::SendAccessGroups>,
             RegMethod<13, &ClientApiSafe::SendAccessGroupForClient>,
             RegMethod<14, &ClientApiSafe::AddItemsToAccessGroup>,
             RegMethod<15, &ClientApiSafe::RemoveItemsFromAccessGroup>,
             RegMethod<16, &ClientApiSafe::AddAccessGroupsToClient>,
             RegMethod<17, &ClientApiSafe::RemoveAccessGroupsFromClient>,
             RegMethod<18, &ClientApiSafe::SendAllAccessedClients>,
             RegMethod<19, &ClientApiSafe::SendAccessCheckResults>,
             RegMethod<20, &ClientApiSafe::SendMessage>,
             ExtApi<&ClientApiSafe::return_result>);

  auto send_message_event() { return EventSubscriber{send_message_event_}; }
  auto send_cloud_event() { return EventSubscriber{send_cloud_event_}; }
  auto send_server_descriptor_event() {
    return EventSubscriber{send_server_descriptor_event_};
  }
  auto request_telemetry_event() {
    return EventSubscriber{request_telemetry_event_};
  }

 private:
  Event<void(AeMessage const& message)> send_message_event_;
  Event<void(ServerDescriptor const& server_descriptor)>
      send_server_descriptor_event_;
  Event<void(Uid const& uid, CloudDescriptor const& cloud)> send_cloud_event_;
  Event<void()> request_telemetry_event_;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_SAFE_H_
