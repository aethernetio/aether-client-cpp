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

#include "aether/access_points/lora_module_access_point.h"

#if AE_SUPPORT_LORA && AE_SUPPORT_GATEWAY

#  include "aether/aether.h"
#  include "aether/server.h"
#  include "aether/lora_modules/ilora_module_driver.h"

#  include "aether/channels/lora_module_channel.h"
#  include "aether/access_points/filter_protocols.h"

namespace ae {
LoraModuleAccessPoint::JoinAction::JoinAction(ActionContext action_context)
    : Action{action_context} {}

UpdateStatus LoraModuleAccessPoint::JoinAction::Update() {
  return UpdateStatus::Result();
}

LoraModuleAccessPoint::LoraModuleAccessPoint(
    ObjPtr<Aether> aether, LoraModuleAdapter::ptr lora_module_adapter,
    Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      lora_module_adapter_{std::move(lora_module_adapter)} {}

ActionPtr<LoraModuleAccessPoint::JoinAction> LoraModuleAccessPoint::Join() {
  return ActionPtr<JoinAction>{*aether_};
}

std::vector<ObjPtr<Channel>> LoraModuleAccessPoint::GenerateChannels(
    ObjPtr<Server> const& server) {
  AE_TELED_DEBUG("Generate lora module channels");
  auto self_ptr = MakePtrFromThis(this);
  return {domain_->CreateObj<LoraModuleChannel>(self_ptr, server)};
}

Aether::ptr const& LoraModuleAccessPoint::aether() const { return aether_; }

GwLoraDevice& LoraModuleAccessPoint::gw_lora_device() {
  if (!gw_lora_device_) {
    gw_lora_device_ = std::make_unique<GwLoraDevice>(
        *aether_);
  }
  return *gw_lora_device_;
}

}  // namespace ae
#endif
