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

#ifndef AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_
#define AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_

#include <memory>

#include "aether/lora_modules/ilora_module_driver.h"
#include "aether/serial_ports/iserial_port.h"
#include "aether/serial_ports/at_comm_support.h"

namespace ae {

class DxSmartLr02LoraModule final : public ILoraModuleDriver {
  AE_OBJECT(DxSmartLr02LoraModule, ILoraModuleDriver, 0)

 protected:
  DxSmartLr02LoraModule() = default;

 public:
  explicit DxSmartLr02LoraModule(LoraModuleInit lora_module_init,
                                 Domain* domain);
  AE_OBJECT_REFLECT()

  bool Init() override;
  bool Start() override;
  bool Stop() override;

 private:
  std::unique_ptr<ISerialPort> serial_;
  std::unique_ptr<AtCommSupport> at_comm_support_;
  bool at_mode_{false};

  static constexpr std::uint16_t kLoraModuleMTU{200};

  kLoraModuleError CheckResponse(std::string const& response,
                                 std::uint32_t const wait_time,
                                 std::string const& error_message);
  void EnterAtMode();
  void LeaveAtMode();
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_DX_SMART_LR02_433_LM_H_