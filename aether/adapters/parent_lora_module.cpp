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

#include "aether/adapters/parent_lora_module.h"
#if AE_SUPPORT_LORA

namespace ae {

#  if defined AE_DISTILLATION
ParentLoraModuleAdapter::ParentLoraModuleAdapter(
    ObjPtr<Aether> aether, IPoller::ptr poller, LoraModuleInit lora_module_init,
    Domain* domain)
    : Adapter{domain},
      aether_{std::move(aether)},
      poller_{std::move(poller)},
      lora_module_init_{std::move(lora_module_init)} {}
#  endif  // AE_DISTILLATION

} /* namespace ae */
#endif
