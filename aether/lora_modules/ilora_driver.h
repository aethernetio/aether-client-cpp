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

#include "aether/obj/obj.h"
#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/lora_modules/lora_driver_types.h"

namespace ae {

class ILoraDriver : public Obj {
  AE_OBJECT(ILoraDriver, Obj, 0)

 protected:
  ILoraDriver() = default;

 public:
  explicit ILoraDriver(LoraInit modem_init, Domain* /*domain*/);
  AE_OBJECT_REFLECT()
  
  ~ILoraDriver() override = default;
  
  LoraInit GetLoraInit();

 private:
  LoraInit lora_init_;
  
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_ILORA_DRIVER_H_