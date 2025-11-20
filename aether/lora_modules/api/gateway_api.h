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

#ifndef AETHER_LORA_MODULES_API_LORA_MODULE_API_H_
#define AETHER_LORA_MODULES_API_LORA_MODULE_API_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA

#  include <array>

#  include "aether/reflect/reflect.h"
#  include "aether/events/events.h"
#  include "aether/types/data_buffer.h"
#  include "aether/api_protocol/api_method.h"
#  include "aether/api_protocol/api_class_impl.h"
#  include "aether/lora_modules/lora_module_driver_types.h"

namespace ae {

struct GwConnection {
  AE_REFLECT_MEMBERS(client_id, server_id)
  std::uint32_t client_id{0};
  std::uint32_t server_id{0};
};

class GatewayApi : public ApiClassImpl<GatewayApi> {
 public:
  explicit GatewayApi(ProtocolContext& protocol_context);

  Method<3, void(GwConnection gwc, DataBuffer data)> packet;

  void PacketImpl(ApiParser& parser, GwConnection gwc, DataBuffer data);

  EventSubscriber<void(GwConnection const& gwc, DataBuffer const& data)>
  packet_event();

  using ApiMethods = ImplList<RegMethod<0x03, &GatewayApi::PacketImpl>>;

 private:
  Event<void(GwConnection const& gwc, DataBuffer const& data)> packet_event_;
};

}  // namespace ae

#endif  // AE_SUPPORT_LORA
#endif  // AETHER_LORA_MODULES_API_LORA_MODULE_API_H_
