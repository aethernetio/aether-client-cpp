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

#ifndef CLOUD_AETHER_CONSTRUCT_MODEM_H_
#define CLOUD_AETHER_CONSTRUCT_MODEM_H_

#include "aether_construct.h"

#if CLOUD_TEST_MODEM

namespace ae::cloud_test {
static constexpr std::string_view kSerialPortModem =
    "COM1";  // Modem serial port
SerialInit serial_init_modem = {std::string(kSerialPortModem),
                                kBaudRate::kBaudRate115200};

ae::ModemInit const modem_init{
    serial_init_modem,             // Serial port
    {},                            // Power save parameters
    {},                            // Base station
    {1, 1, 1, 1},                  // Pin code
    false,                         // Use pin
    ae::kModemMode::kModeNbIot,    // Modem mode
    "00001",                       // Operator code
    "",                            // Operator long name
    "internet",                    // APN
    "user",                        // APN user
    "password",                    // APN pass
    ae::kAuthType::kAuthTypeNone,  // Auth type
    false,                         // Use auth
    "",                            // Auth user
    "",                            // Auth pass
    "",                            // SSL cert
    false                          // Use SSL
};
static RcPtr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#  if defined AE_DISTILLATION
          .AdaptersFactory([](AetherAppContext const& context) {
            auto adapter_registry =
                context.domain().CreateObj<AdapterRegistry>();
            adapter_registry->Add(context.domain().CreateObj<ae::ModemAdapter>(
                ae::GlobalId::kModemAdapter, context.aether(), modem_init));
            return adapter_registry;
          })
#  endif
  );
}
}  // namespace ae::cloud_test

#endif
#endif  // CLOUD_AETHER_CONSTRUCT_MODEM_H_
