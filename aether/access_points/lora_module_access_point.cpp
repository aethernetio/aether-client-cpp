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

#include "aether/aether.h"
#include "aether/lora_modules/ilora_module_driver.h"

#include "aether/channels/lora_module_channel.h"

namespace ae {
LoraModuleConnectAction::LoraModuleConnectAction(ActionContext action_context,
                                       [[maybe_unused]] ILoraModuleDriver& driver)
    : Action{action_context} {}

UpdateStatus LoraModuleConnectAction::Update() {
  // TODO: Implement asynchronous modem connection
  return UpdateStatus::Result();
}

LoraModuleAccessPoint::LoraModuleAccessPoint(ObjPtr<Aether> aether,
                                   LoraModuleAdapter::ptr lora_module_adapter,
                                   Domain* domain)
    : AccessPoint{domain},
      aether_{std::move(aether)},
      lora_module_adapter_{std::move(lora_module_adapter)} {}

ActionPtr<LoraModuleConnectAction> LoraModuleAccessPoint::Connect() {
  return ActionPtr<LoraModuleConnectAction>{*aether_.as<Aether>(),
                                       lora_module_adapter_->lora_module_driver()};
}

ILoraModuleDriver& LoraModuleAccessPoint::lora_module_driver() {
  return lora_module_adapter_->lora_module_driver();
}

std::vector<ObjPtr<Channel>> LoraModuleAccessPoint::GenerateChannels(
    std::vector<UnifiedAddress> const& endpoints) {
  std::vector<ObjPtr<Channel>> channels;
  channels.reserve(endpoints.size());
  Aether::ptr aether = aether_;
  LoraModuleAccessPoint::ptr self = MakePtrFromThis(this);
  for (auto const& endpoint : endpoints) {
    channels.emplace_back(
        domain_->CreateObj<LoraModuleChannel>(aether, self, endpoint));
  }
  return channels;
}

}  // namespace ae
