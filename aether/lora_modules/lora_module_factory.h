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

#include "aether/config.h"
#if AE_SUPPORT_LORA
#  include <memory>

#  include "aether/poller/poller.h"
#  include "aether/actions/action_context.h"
#  include "aether/lora_modules/ilora_module_driver.h"

namespace ae {
class LoraModuleDriverFactory {
 public:
  static std::unique_ptr<ILoraModuleDriver> CreateLoraModule(
      ActionContext action_context, IPoller::ptr const& poller,
      LoraModuleInit lora_module_init);
};
}  // namespace ae

#endif
#endif  // AETHER_LORA_MODULES_LORA_MODULE_FACTORY_H_
