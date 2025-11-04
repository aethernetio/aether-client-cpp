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
#if AE_SUPPORT_MODEMS

// IWYU pragma: begin_keeps
#  include "aether/modems/bg95_at_modem.h"
#  include "aether/modems/sim7070_at_modem.h"
#  include "aether/modems/thingy91x_at_modem.h"
// IWYU pragma: end_keeps

namespace ae {

std::unique_ptr<IModemDriver> ModemDriverFactory::CreateModem(
    ActionContext action_context, IPoller::ptr const& poller,
    ModemInit modem_init) {
#  if AE_ENABLE_BG95
  return std::make_unique<Bg95AtModem>(action_context, poller,
                                       std::move(modem_init));
#  elif AE_ENABLE_SIM7070
  return std::make_unique<Sim7070AtModem>(action_context, poller,
                                          std::move(modem_init));
#  elif AE_ENABLE_THINGY91X
  return std::make_unique<Thingy91xAtModem>(action_context, poller,
                                            std::move(modem_init));
#  else
#    error "No modem implementation is enabled"
#  endif
}
}  // namespace ae
#endif
