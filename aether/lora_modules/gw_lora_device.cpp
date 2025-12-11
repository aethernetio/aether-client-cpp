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

#include "aether/lora_modules/gw_lora_device.h"

#include "aether/tele/tele.h"

namespace ae {
namespace gw_lora_device_internal {
class GwStreamWriteAction final : public StreamWriteAction {
 public:
  explicit GwStreamWriteAction(ActionContext action_context)
      : StreamWriteAction{action_context} {
    state_ = State::kDone;
  }
};
}  // namespace gw_lora_device_internal

GwLoraDevice::GwLoraDevice(ActionContext action_context)
    : action_context_{action_context},
      device_id_{},
      gateway_api_{protocol_context_},
      gateway_client_api_{protocol_context_} {}

GwLoraDevice::~GwLoraDevice() {}

ActionPtr<StreamWriteAction> GwLoraDevice::ToServer(ClientId client_id,
                                                   ServerId server_id,
                                                   DataBuffer&& data) {
  auto api = ApiContext{gateway_api_};
  api->to_server_id(client_id, server_id, std::move(data));
  DataBuffer packet = std::move(api);

  AE_TELED_DEBUG("Publish from device_id {} data {}",
                 static_cast<int>(device_id_), packet);

  return ActionPtr<gw_lora_device_internal::GwStreamWriteAction>{
      action_context_};
}

ActionPtr<StreamWriteAction> GwLoraDevice::ToServer(
    ClientId client_id, ServerEndpoints const& server_endpoints,
    DataBuffer&& data) {
  auto api = ApiContext{gateway_api_};
  api->to_server(client_id, server_endpoints, std::move(data));
  DataBuffer packet = std::move(api);

  AE_TELED_DEBUG("Publish from device_id {} data {}",
                 static_cast<int>(device_id_), packet);

  return ActionPtr<gw_lora_device_internal::GwStreamWriteAction>{
      action_context_};
}

GwLoraDevice::FromServerEvent::Subscriber GwLoraDevice::from_server_event() {
  return gateway_client_api_.from_server_event();
}
}  // namespace ae
