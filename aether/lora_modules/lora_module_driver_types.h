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

#ifndef AETHER_LORA_MODULES_LORA_DRIVER_TYPES_H_
#define AETHER_LORA_MODULES_LORA_DRIVER_TYPES_H_

#include "aether/reflect/reflect.h"
#include "aether/serial_ports/serial_port_types.h"

namespace ae {
enum class kLoraModuleError : std::int8_t {
  kNoError = 0,
  kSerialPortError = -1,
  kAtCommandError = -2
};

// ========================lora module init=====================================
struct LoraModuleInit {
  AE_REFLECT_MEMBERS(serial_init, lora_my_adress, lora_bs_adress, lora_channel,
                     lora_config)
  SerialInit serial_init;
  std::uint16_t lora_my_adress{0};
  std::uint16_t lora_bs_adress{0};
  std::uint8_t lora_channel{0};
  std::uint8_t lora_config{0};
};

}  // namespace ae

#endif  // AETHER_LORA_MODULES_LORA_DRIVER_TYPES_H_