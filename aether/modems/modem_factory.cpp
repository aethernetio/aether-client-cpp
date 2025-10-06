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

#include "aether/modems/modem_factory.h"

// IWYU pragma: begin_keeps
// #include "aether/modems/bg95_at_modem.h"
#include "aether/modems/sim7070_at_modem.h"
#include "aether/modems/thingy91x_at_modem.h"
// IWYU pragma: end_keeps

namespace ae {

std::unique_ptr<IModemDriver> ModemDriverFactory::CreateModem(
    ModemInit modem_init) {
#if AE_MODEM_BG95_ENABLED == 1
  // return domain->CreateObj<Bg95AtModem>(modem_init);
#elif AE_MODEM_SIM7070_ENABLED == 1
  return std::make_unique<Sim7070AtModem>(std::move(modem_init));
#elif AE_MODEM_THINGY91X_ENABLED == 1
  return std::make_unique<Thingy91xAtModem>(std::move(modem_init));
#endif
}

}  // namespace ae
