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

#ifndef AETHER_LORA_MODULES_EBYTE22_LORA_MODULE_H_
#define AETHER_LORA_MODULES_EBYTE22_LORA_MODULE_H_

#include <memory>

#include "aether/lora_modules/ilora_module_driver.h"
#include "aether/serial_ports/iserial_port.h"

namespace ae {

class EbyteE22LoraModule final : public ILoraModuleDriver {
  AE_OBJECT(EbyteE22LoraModule, ILoraModuleDriver, 0)

 protected:
  EbyteE22LoraModule() = default;

 public:
  explicit EbyteE22LoraModule(LoraModuleInit lora_module_init, Domain* domain);
  AE_OBJECT_REFLECT()

 private:
  std::unique_ptr<ISerialPort> serial_;

  static constexpr std::uint16_t kLoraModuleMTU{200};
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_EBYTE22_LORA_MODULE_H_