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

#ifndef AETHER_TRANSPORT_GATEWAY_GATEWAY_DEVICE_H_
#define AETHER_TRANSPORT_GATEWAY_GATEWAY_DEVICE_H_

#include "aether/config.h"

#if AE_SUPPORT_GATEWAY

#  include "aether/types/server_id.h"
#  include "aether/types/client_id.h"
#  include "aether/types/data_buffer.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/gateway_api/gateway_api.h"
#  include "aether/gateway_api/server_endpoints.h"
#  include "aether/write_action/write_action.h"

namespace ae {
/**
 * \brief Should implement using GatewayApi with some transport protocol.
 */
class IGatewayDevice {
 public:
  using FromServerEvent =
      decltype(std::declval<GatewayClientApi>().from_server_event())::EventType;

  virtual ~IGatewayDevice() = default;

  /**
   * \brief Write data through gateway to server by id.
   */
  virtual ActionPtr<WriteAction> ToServer(ClientId client_id,
                                          ServerId server_id,
                                          DataBuffer&& data) = 0;
  /**
   * \brief Write data through gateway to server by endpoints.
   */
  virtual ActionPtr<WriteAction> ToServer(
      ClientId client_id, ServerEndpoints const& server_endpoints,
      DataBuffer&& data) = 0;

  /**
   * \brief Get data from server by endpoints.
   */
  virtual FromServerEvent::Subscriber from_server_event() = 0;
};
}  // namespace ae

#endif
#endif  // AETHER_TRANSPORT_GATEWAY_GATEWAY_DEVICE_H_
