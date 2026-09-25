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
#    if defined(ESP_PLATFORM)
#      if !defined(AE_MODEM_UART_PORT) || !defined(AE_MODEM_UART_TX_GPIO) || \
          !defined(AE_MODEM_UART_RX_GPIO)
#        error "Select a modem board USER_CONFIG with UART port and pins"
#      endif
static SerialInit const serial_init_modem{
    .port_name = AE_MODEM_UART_PORT,
    .baud_rate = kBaudRate::kBaudRate115200,
    .tx_io_num = AE_MODEM_UART_TX_GPIO,
    .rx_io_num = AE_MODEM_UART_RX_GPIO,
    .rts_io_num = -1,
    .cts_io_num = -1};
#    else
static constexpr std::string_view kSerialPortModem = "COM28";
SerialInit serial_init_modem = {std::string(kSerialPortModem),
                                kBaudRate::kBaudRate115200};
#    endif

static ae::ModemInit const modem_init{
    serial_init_modem,  ///< Platform serial port configuration.
    {},                 ///< Requested power-saving configuration.
    {},                 ///< Base-station configuration data.
    1111,               ///< Numeric SIM PIN used when use_pin is enabled.
    false,              ///< Whether to submit the configured SIM PIN.
    ae::kModemMode::kModeNbIot,  ///< Requested radio access mode.
    "25001",            ///< Numeric operator code; an empty value permits
                        ///< automatic selection.
    "",                 ///< Operator name, preferred over operator_code
                        ///< when supported.
    "internet.mts.ru",  ///< Access point name for packet data.
    "mts",              ///< APN authentication user name.
    "mts",              ///< APN authentication password.
    ae::kAuthType::kAuthTypeNone,  ///< Requested APN authentication method.
    false,  ///< Optional authentication flag; support is driver-specific.
    "",     ///< Optional authentication user name.
    "",     ///< Optional authentication password.
    "",     ///< Optional SSL certificate configuration.
    false   ///< Optional SSL flag; support is driver-specific.
};

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
