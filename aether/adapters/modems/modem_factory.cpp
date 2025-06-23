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

#include "aether/adapters/modems/modem_factory.h"

#include "aether/adapters/modems/bg95_at_modem.h"
#include "aether/adapters/modems/sim7070_at_modem.h"


namespace ae {

std::unique_ptr<IModemDriver> ModemDriverFactory::CreateModem(
    ModemInit modem_init) {
#if defined AE_MODEM_BG95_ENABLED
  return std::make_unique<Bg95AtModem>(modem_init);
#elif defined AE_MODEM_SIM7070_ENABLED
  return std::make_unique<Sim7070AtModem>(modem_init);
#endif
}

}  // namespace ae