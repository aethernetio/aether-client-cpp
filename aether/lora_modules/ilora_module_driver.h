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

#ifndef AETHER_LORA_MODULES_ILORA_MODULE_DRIVER_H_
#define AETHER_LORA_MODULES_ILORA_MODULE_DRIVER_H_

#include "aether/config.h"

#if AE_SUPPORT_LORA

#  include <string>
#  include <cstdint>

#  include "aether/types/address.h"
#  include "aether/events/events.h"
#  include "aether/actions/action.h"
#  include "aether/types/data_buffer.h"
#  include "aether/actions/action_ptr.h"
#  include "aether/actions/notify_action.h"
#  include "aether/actions/promise_action.h"
#  include "aether/lora_modules/lora_module_driver_types.h"

namespace ae {
class ILoraModuleDriver {
 public:
  using LoraModuleOperation = NotifyAction;
  using WriteOperation = NotifyAction;
  using OpenNetworkOperation = PromiseAction<ConnectionLoraIndex>;
  using DataEvent = Event<void(ConnectionLoraIndex, DataBuffer const& data)>;

  virtual ~ILoraModuleDriver() = default;

  virtual ActionPtr<LoraModuleOperation> Start() = 0;
  virtual ActionPtr<LoraModuleOperation> Stop() = 0;
  virtual ActionPtr<OpenNetworkOperation> OpenNetwork(Protocol protocol,
                                                      std::string const& host,
                                                      std::uint16_t port) = 0;
  virtual ActionPtr<LoraModuleOperation> CloseNetwork(
      ConnectionLoraIndex connect_index) = 0;
  virtual ActionPtr<LoraModuleOperation> WritePacket(
      ConnectionLoraIndex connect_index, DataBuffer const& data) = 0;
  virtual DataEvent::Subscriber data_event() = 0;

  virtual ActionPtr<LoraModuleOperation> SetPowerSaveParam(
      LoraPowerSaveParam const& psp) = 0;
  virtual ActionPtr<LoraModuleOperation> PowerOff() = 0;
};

} /* namespace ae */

#endif
#endif  // AETHER_LORA_MODULES_ILORA_MODULE_DRIVER_H_
