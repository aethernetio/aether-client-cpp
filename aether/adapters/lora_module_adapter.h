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

#ifndef AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_
#define AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA
#  include <cstdint>

#  include "aether/events/events.h"

#  include "aether/lora_modules/ilora_module_driver.h"
#  include "aether/adapters/parent_lora_module.h"
#  include "aether/access_points/access_point.h"

#  define LORA_MODULE_TCP_TRANSPORT_ENABLED 1

namespace ae {
class LoraModuleAdapter : public ParentLoraModuleAdapter {
  AE_OBJECT(LoraModuleAdapter, ParentLoraModuleAdapter, 0)

  LoraModuleAdapter() = default;

 public:
#  ifdef AE_DISTILLATION
  LoraModuleAdapter(ObjPtr<Aether> aether, IPoller::ptr poller,
                    LoraModuleInit lora_module_init, Domain* domain);
#  endif  // AE_DISTILLATION

  ~LoraModuleAdapter() override;

  AE_OBJECT_REFLECT(AE_MMBRS(access_point_))

  std::vector<AccessPoint::ptr> access_points() override;

  ILoraModuleDriver& lora_module_driver();

 private:
  bool connected_{false};

  std::unique_ptr<ILoraModuleDriver> lora_module_driver_;
  AccessPoint::ptr access_point_;
};

}  // namespace ae
#endif
#endif  // AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_
