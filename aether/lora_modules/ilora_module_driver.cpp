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

#include "aether/lora_modules/ilora_module_driver.h"

namespace ae {

ILoraModuleDriver::ILoraModuleDriver(IPoller::ptr /* poller */,
                                     LoraModuleInit lora_module_init,
                                     Domain* domain)
    : Obj{domain}, lora_module_init_{std::move(lora_module_init)} {}

bool ILoraModuleDriver::Init() { return {}; }
bool ILoraModuleDriver::Start() { return {}; }
bool ILoraModuleDriver::Stop() { return {}; }
ConnectionLoraIndex ILoraModuleDriver::OpenNetwork(Protocol /*protocol*/,
                                          std::string const& /*host*/,
                                          std::uint16_t /*port*/) {
  return {};
}
void ILoraModuleDriver::CloseNetwork(ConnectionLoraIndex /*connect_index*/) {}
void ILoraModuleDriver::WritePacket(ConnectionLoraIndex /*connect_index*/,
                               DataBuffer const& /*data*/) {}
DataBuffer ILoraModuleDriver::ReadPacket(ConnectionLoraIndex /*connect_index*/,
                                    Duration /*timeout*/) {
  return {};
}

bool ILoraModuleDriver::SetPowerSaveParam(std::string const& /*psp*/) {
  return {};
}
bool ILoraModuleDriver::PowerOff() { return {}; }

LoraModuleInit ILoraModuleDriver::GetLoraModuleInit() { return lora_module_init_; }
} /* namespace ae */