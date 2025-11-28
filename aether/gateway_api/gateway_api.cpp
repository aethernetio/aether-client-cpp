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

#include "aether/gateway_api/gateway_api.h"

#if AE_SUPPORT_GATEWAY

namespace ae {
GatewayApi::GatewayApi(ProtocolContext& protocol_context)
    : ApiClass{protocol_context},
      packet_to_server_id{protocol_context},
      packet_to_server{protocol_context} {}

GatewayClientApi::GatewayClientApi(ProtocolContext& protocol_context)
    : ApiClass{protocol_context} {}

void GatewayClientApi::FromServerId(ClientId client_id, ServerId server_id,
                                    DataBuffer data) {
  from_server_id_event_.Emit(client_id, server_id, data);
}

void GatewayClientApi::FromServer(ClientId client_id,
                                  std::uin64_t descriptor_hash,
                                  DataBuffer data) {
  from_server_event_.Emit(client_id, descriptor_hash, data);
}

}  // namespace ae

#endif  // AE_SUPPORT_LORA
