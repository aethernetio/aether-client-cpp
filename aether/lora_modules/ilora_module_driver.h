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

#ifndef AETHER_LORA_MODULES_ILORA_DRIVER_H_
#define AETHER_LORA_MODULES_ILORA_DRIVER_H_

#include <string>
#include <cstdint>

#include "aether/obj/obj.h"
#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/lora_modules/lora_module_driver_types.h"
#include "aether/poller/poller.h"

namespace ae {
class ILoraModuleDriver : public Obj {
  AE_OBJECT(ILoraModuleDriver, Obj, 0)

 protected:
  ILoraModuleDriver() = default;

 public:
  ILoraModuleDriver(IPoller::ptr poller, LoraModuleInit lora_module_init,
                    Domain* /* domain */);
  ~ILoraModuleDriver() override = default;
  AE_OBJECT_REFLECT(AE_MMBRS(lora_module_init_))

  virtual bool Init();
  virtual bool Start();
  virtual bool Stop();
  virtual ConnectionLoraIndex OpenNetwork(Protocol /*protocol*/,
                                          std::string const& /*host*/,
                                          std::uint16_t /*port*/);
  virtual void CloseNetwork(ConnectionLoraIndex /*connect_index*/);
  virtual void WritePacket(ConnectionLoraIndex /*connect_index*/,
                           DataBuffer const& /*data*/);
  virtual DataBuffer ReadPacket(ConnectionLoraIndex /*connect_index*/,
                                Duration /*timeout*/);

  virtual bool SetPowerSaveParam(std::string const& /*psp*/);
  virtual bool PowerOff();

  LoraModuleInit GetLoraModuleInit();

 private:
  LoraModuleInit lora_module_init_;
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_ILORA_DRIVER_H_
