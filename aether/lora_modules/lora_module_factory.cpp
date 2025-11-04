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
#if AE_SUPPORT_LORA

//// IWYU pragma: begin_keeps
#  include "aether/lora_modules/ebyte_e22_400_lm.h"
#  include "aether/lora_modules/dx_smart_lr02_433_lm.h"
// IWYU pragma: end_keeps

namespace ae {

std::unique_ptr<ILoraModuleDriver> LoraModuleDriverFactory::CreateLoraModule(
    ActionContext action_context, IPoller::ptr const& poller,
    LoraModuleInit lora_module_init) {
#  if AE_ENABLE_EBYTE_E22_400
  return std::make_unique<EbyteE22LoraModule>(action_context, poller,
                                              std::move(lora_module_init));
#  elif AE_ENABLE_DX_SMART_LR02_433_LM
  return std::make_unique<DxSmartLr02LoraModule>(action_context, poller,
                                                 std::move(lora_module_init));
#  else
#    error "No supported LoRa module driver found"
#  endif
}

}  // namespace ae
#endif
