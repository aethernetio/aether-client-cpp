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

#include "aether/lora_modules/lora_module_factory.h"

#include "aether/lora_modules/ebyte_e22_400_lm.h"
#include "aether/lora_modules/dx_smart_lr02_433_lm.h"

namespace ae {

ILoraModuleDriver::ptr LoraModuleDriverFactory::CreateLoraModule(
    LoraModuleAdapter& adapter, IPoller::ptr poller,
    LoraModuleInit lora_module_init, Domain* domain) {
#if AE_LORA_MODULE_EBYTE22_ENABLED == 1
  return domain->CreateObj<EbyteE22LoraModule>(adapter, poller, lora_module_init);
#elif AE_LORA_MODULE_DXSMART_LR02_ENABLED == 1
  return domain->CreateObj<DxSmartLr02LoraModule>(adapter, poller,
                                                  lora_module_init);
#endif
}

}  // namespace ae
