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

/**
 * @file modem_factory.h
 * @brief Factory for the modem driver selected by compile-time configuration.
 */

#ifndef AETHER_MODEMS_MODEM_FACTORY_H_
#define AETHER_MODEMS_MODEM_FACTORY_H_

#include "aether/config.h"

#if AE_SUPPORT_MODEMS
#  include <memory>

#  include "aether/ae_context.h"
#  include "aether/poller/poller.h"
#  include "aether/modems/imodem_driver.h"

namespace ae {
/**
 * @brief Construct the modem driver enabled in the build configuration.
 */
class ModemDriverFactory {
 public:
  /**
   * @brief Create a driver and start its serial initialization.
   * @param ae_context Non-owning runtime context that must outlive the driver.
   * @param poller Poller used by the platform serial port implementation.
   * @param modem_init Serial, SIM, network, and power configuration.
   * @return Exclusive ownership of the selected modem driver.
   * @note Selection priority is BG95, SIM7070, then Thingy91X. Enabling no
   * driver produces a compile-time error; this factory does not detect attached
   * hardware.
   */
  static std::unique_ptr<IModemDriver> CreateModem(AeContext const& ae_context,
                                                   IPoller::ptr const& poller,
                                                   ModemInit modem_init);
};
}  // namespace ae

#endif
#endif  // AETHER_MODEMS_MODEM_FACTORY_H_
