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

class ClientApiSafe : public ApiClassImpl<ClientApiSafe> {
 public:
  explicit ClientApiSafe(ProtocolContext& protocol_context);

  void SendMessages(ApiParser& parser, std::vector<AeMessage> messages);

  void SendServerDescriptor(ApiParser& parser,
                            ServerDescriptor server_descriptor);
  void SendServerDescriptors(ApiParser& parser,
                             std::vector<ServerDescriptor> server_descriptors);
  void SendCloud(ApiParser& parser, Uid uid, CloudDescriptor cloud);
  void SendClouds(ApiParser& parser, std::vector<UidAndCloudDescriptor> clouds);

  void RequestTelemetry(ApiParser& parser);

  ReturnResultApi return_result;

  using ApiMethods =
      ImplList<RegMethod<6, &ClientApiSafe::SendMessages>,
               RegMethod<7, &ClientApiSafe::SendServerDescriptor>,
               RegMethod<8, &ClientApiSafe::SendServerDescriptors>,
               RegMethod<9, &ClientApiSafe::SendCloud>,
               RegMethod<10, &ClientApiSafe::SendClouds>,
               RegMethod<11, &ClientApiSafe::RequestTelemetry>,
               ExtApi<&ClientApiSafe::return_result>>;

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
  Event<void(Uid uid, CloudDescriptor const& cloud)> send_cloud_event_;
  Event<void()> request_telemetry_event_;
};
}  // namespace ae

#endif  // AETHER_WORK_CLOUD_API_CLIENT_API_CLIENT_API_SAFE_H_
