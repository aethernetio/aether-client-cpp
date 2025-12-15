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

#ifndef AETHER_LORA_MODULES_GW_LORA_DEVICE_H_
#define AETHER_LORA_MODULES_GW_LORA_DEVICE_H_

#include "aether/config.h"

#if AE_SUPPORT_GATEWAY

#include <cstdint>

#include "aether/types/server_id.h"
#include "aether/types/client_id.h"
#include "aether/reflect/reflect.h"
#include "aether/gateway_api/gateway_api.h"
#include "aether/transport/gateway/gateway_device.h"


namespace ae {
using DeviceId = std::uint8_t;

static constexpr std::uint16_t kLoraModuleAddress = 2;

struct LoraPacket {
  AE_REFLECT_MEMBERS(address, length, data, crc)
  std::uint16_t address{kLoraModuleAddress};
  std::size_t length{0};
  DataBuffer data;
  std::uint32_t crc{0};
};

class GwLoraDevice final : public IGatewayDevice {
 public:
  explicit GwLoraDevice(ActionContext action_context);
  ~GwLoraDevice() override;

  ActionPtr<StreamWriteAction> ToServer(ClientId client_id, ServerId server_id,
                                        DataBuffer&& data) override;

  ActionPtr<StreamWriteAction> ToServer(ClientId client_id,
                                        ServerEndpoints const& server_endpoints,
                                        DataBuffer&& data) override;

  FromServerEvent::Subscriber from_server_event() override;

 private:
  ActionContext action_context_;
  DeviceId device_id_;

  ProtocolContext protocol_context_;
  GatewayApi gateway_api_;
  GatewayClientApi gateway_client_api_;
};
}  // namespace ae

#endif
#endif  // AETHER_LORA_MODULES_GW_LORA_DEVICE_H_
