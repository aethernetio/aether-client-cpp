/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_GATEWAY_API_GATEWAY_API_H_
#define AETHER_GATEWAY_API_GATEWAY_API_H_

#include "aether/config.h"

#if AE_SUPPORT_GATEWAY

#  include <cstdint>

#  include "aether/events/events.h"
#  include "aether/types/server_id.h"
#  include "aether/types/client_id.h"
#  include "aether/types/data_buffer.h"
#  include "aether/api_protocol/api_method.h"
#  include "aether/api_protocol/api_class_impl.h"

#  include "aether/gateway_api/server_endpoints.h"

namespace ae {
class GatewayApi : public ApiClass {
 public:
  explicit GatewayApi(ProtocolContext& protocol_context);

  Method<3, void(ClientId client_id, ServerId server_id, DataBuffer data)>
      to_server_id;
  Method<4, void(ClientId client_id, ServerEndpoints server_endpoints,
                 DataBuffer data)>
      to_server;
};

class GatewayClientApi : public ApiClassImpl<GatewayClientApi> {
 public:
  explicit GatewayClientApi(ProtocolContext& protocol_context);

  void FromServer(ClientId client_id, DataBuffer data);

  AE_METHODS(RegMethod<3, &GatewayClientApi::FromServer>);

  auto from_server_event() { return EventSubscriber{from_server_event_}; }

 private:
  Event<void(ClientId client_id, DataBuffer const& data)> from_server_event_;
};

}  // namespace ae

#endif  // AE_SUPPORT_GATEWAY
#endif  // AETHER_GATEWAY_API_GATEWAY_API_H_
