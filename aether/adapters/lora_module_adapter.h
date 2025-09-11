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

#define LORA_MODULE_ADAPTER_ENABLED 1

#include <cstdint>

#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/events/event_subscription.h"

#include "aether/lora_modules/ilora_module_driver.h"
//#include "aether/adapters/parent_modem.h"

namespace ae {
class LoraModuleAdapter;
namespace lora_module_adapter_internal {
} // lora_module_adapter_internal


}  // namespace ae

#endif  // AETHER_ADAPTERS_LORA_MODULE_ADAPTER_H_