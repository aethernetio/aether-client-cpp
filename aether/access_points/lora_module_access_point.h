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

#ifndef AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA && AE_SUPPORT_GATEWAY

#  include "aether/obj/obj.h"
#  include "aether/actions/action.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/adapters/lora_module_adapter.h"
#  include "aether/access_points/access_point.h"
#  include "aether/lora_modules/gw_lora_device.h"

namespace ae {
class LoraModuleAccessPoint final : public AccessPoint {
  AE_OBJECT(LoraModuleAccessPoint, AccessPoint, 0)
  LoraModuleAccessPoint() = default;

 public:
  class JoinAction : public Action<JoinAction> {
   public:
    explicit JoinAction(ActionContext action_context);
    UpdateStatus Update();
  };
  
  LoraModuleAccessPoint(ObjPtr<Aether> aether,
                        LoraModuleAdapter::ptr lora_module_adapter,
                        Domain* domain);

  AE_OBJECT_REFLECT(AE_MMBRS(lora_module_adapter_));

  ActionPtr<JoinAction> Join();

  std::vector<ObjPtr<Channel>> GenerateChannels(
      Server::ptr const& server) override;

  Aether::ptr const& aether() const;
  GwLoraDevice& gw_lora_device();
 private:
  Aether::ptr aether_;
  Adapter::ptr lora_module_adapter_;
  std::unique_ptr<GwLoraDevice> gw_lora_device_;
};
}  // namespace ae
#endif
#endif  // AETHER_ACCESS_POINTS_LORA_MODULE_ACCESS_POINT_H_
