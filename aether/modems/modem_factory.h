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

#ifndef AETHER_MODEMS_MODEM_FACTORY_H_
#define AETHER_MODEMS_MODEM_FACTORY_H_

#include <memory>

#include "aether/modems/imodem_driver.h"

#define AE_MODEM_BG95_ENABLED 0
#define AE_MODEM_SIM7070_ENABLED 1
#define AE_MODEM_THINGY91X_ENABLED 0

// check if any mode is enabled
#if (AE_MODEM_SIM7070_ENABLED == 1) || (AE_MODEM_BG95_ENABLED == 1) || \
    (AE_MODEM_THINGY91X_ENABLED == 1)
#  define AE_MODEM_ENABLED 1

namespace ae {
class ModemDriverFactory {
 public:
  static std::unique_ptr<IModemDriver> CreateModem(ModemInit modem_init);
};
}  // namespace ae

#endif

#endif  // AETHER_MODEMS_MODEM_FACTORY_H_
