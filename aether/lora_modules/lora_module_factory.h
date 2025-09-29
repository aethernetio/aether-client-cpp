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

#ifndef AETHER_LORA_MODULES_LORA_MODULE_FACTORY_H_
#define AETHER_LORA_MODULES_LORA_MODULE_FACTORY_H_

#include <memory>

#include "aether/lora_modules/ilora_module_driver.h"
#include "aether/adapters/lora_module_adapter.h"

#define AE_LORA_MODULE_EBYTE_E22_ENABLED 0
#define AE_LORA_MODULE_DXSMART_LR02_ENABLED 1

// check if any mode is enabled
#if (AE_LORA_MODULE_EBYTE_E22_ENABLED == 1) || \
    (AE_LORA_MODULE_DXSMART_LR02_ENABLED == 1)
#  define AE_LORA_MODULE_ENABLED 1

namespace ae {
class LoraModuleDriverFactory {
 public:
  static ILoraModuleDriver::ptr CreateLoraModule(
      LoraModuleAdapter& adapter, IPoller::ptr poller,
      LoraModuleInit lora_module_init, Domain* domain);
};
}  // namespace ae

#endif

#endif  // AETHER_LORA_MODULES_LORA_MODULE_FACTORY_H_
