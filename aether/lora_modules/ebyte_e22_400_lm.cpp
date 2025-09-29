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

#include "aether/lora_modules/ebyte_e22_400_lm.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {

EbyteE22LoraModule::EbyteE22LoraModule(LoraModuleAdapter& adapter,
                                       IPoller::ptr poller,
                                       LoraModuleInit lora_module_init,
                                       Domain* domain)
    : ILoraModuleDriver{std::move(poller), std::move(lora_module_init), domain},
      adapter_{&adapter} {
  serial_ = SerialPortFactory::CreatePort(*adapter_->aether_.as<Aether>(),
                                          adapter_->poller_,
                                          GetLoraModuleInit().serial_init);
};

}  // namespace ae
