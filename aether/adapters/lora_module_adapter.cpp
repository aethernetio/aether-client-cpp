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

#include "aether/adapters/lora_module_adapter.h"
#if AE_SUPPORT_LORA

#  include "aether/aether.h"
#  include "aether/lora_modules/lora_module_factory.h"
#  include "aether/access_points/lora_module_access_point.h"

#  include "aether/adapters/adapter_tele.h"

namespace ae {

#  if defined AE_DISTILLATION
LoraModuleAdapter::LoraModuleAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                                     LoraModuleInit lora_module_init,
                                     Domain* domain)
    : ParentLoraModuleAdapter{std::move(aether), std::move(poller),
                              std::move(lora_module_init), domain} {
  AE_TELED_DEBUG("Lora module instance created!");
}
#  endif  // AE_DISTILLATION

LoraModuleAdapter::~LoraModuleAdapter() {
  if (connected_) {
    AE_TELED_DEBUG("Lora module instance deleted!");
    connected_ = false;
  }
}

std::vector<AccessPoint::ptr> LoraModuleAdapter::access_points() {
  if (!access_point_) {
    Aether::ptr aether = aether_;
    LoraModuleAdapter::ptr self = MakePtrFromThis(this);
    assert(self);
    access_point_ =
        domain_->CreateObj<LoraModuleAccessPoint>(aether, std::move(self));
  }
  return {access_point_};
}

ILoraModuleDriver& LoraModuleAdapter::lora_module_driver() {
  if (!lora_module_driver_) {
    lora_module_driver_ = LoraModuleDriverFactory::CreateLoraModule(
        *aether_.as<Aether>(), poller_, lora_module_init_);
  }
  return *lora_module_driver_;
}
}  // namespace ae
#endif
