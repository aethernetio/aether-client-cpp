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

#ifndef EXAMPLES_COMMON_AETHER_CONSTRUCT_MODEM_H_
#define EXAMPLES_COMMON_AETHER_CONSTRUCT_MODEM_H_

#include "aether_construct.h"

#if AE_EXAMPLE_MODEM
#  if !AE_SUPPORT_MODEMS
#    error "Modem support is required for cloud test modem"
#  else

namespace ae::examples {
static constexpr std::string_view kSerialPortModem = "COM1";
SerialInit serial_init_modem = {std::string(kSerialPortModem),
                                kBaudRate::kBaudRate115200};

static ae::ModemInit const modem_init{serial_init_modem,
                                      {},
                                      {},
                                      1111,
                                      false,
                                      ae::kModemMode::kModeNbIot,
                                      "00001",
                                      "",
                                      "internet",
                                      "user",
                                      "password",
                                      ae::kAuthType::kAuthTypeNone,
                                      false,
                                      "",
                                      "",
                                      "",
                                      false};

static std::unique_ptr<AetherApp> construct_aether_app() {
  return AetherApp::Construct(
      AetherAppContext{}
#    if defined AE_DISTILLATION
          .AddAdapterFactory([](AetherAppContext const& context) {
            return ModemAdapter::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    ae::GlobalId::kModemAdapter),
                context.aether(), context.poller(), modem_init);
          })
#    endif
  );
}
}  // namespace ae::examples

#  endif
#endif

#endif  // EXAMPLES_COMMON_AETHER_CONSTRUCT_MODEM_H_
