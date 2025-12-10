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

#ifndef AETHER_CHANNELS_LORA_MODULE_CHANNEL_H_
#define AETHER_CHANNELS_LORA_MODULE_CHANNEL_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA
#  include "aether/channels/channel.h"
#  include "aether/access_points/lora_module_access_point.h"

namespace ae {
class Aether;

class LoraModuleChannel final : public Channel {
  AE_OBJECT(LoraModuleChannel, Channel, 0)
  LoraModuleChannel() = default;

 public:
  LoraModuleChannel(ObjPtr<Aether> aether,
                    LoraModuleAccessPoint::ptr access_point, Endpoint address,
                    Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(access_point_))

  ActionPtr<TransportBuilderAction> TransportBuilder() override;

  Duration TransportBuildTimeout() const override;

 private:
  Obj::ptr aether_;
  LoraModuleAccessPoint::ptr access_point_;
};
}  // namespace ae
#endif
#endif  // AETHER_CHANNELS_LORA_MODULE_CHANNEL_H_
