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

#ifndef CONFIG_USER_CONFIG_FIREBEETLE_C6_SIM7070_H_
#define CONFIG_USER_CONFIG_FIREBEETLE_C6_SIM7070_H_
#include "config/user_config_modem.h"
#if defined(ESP_PLATFORM)
#  include "sdkconfig.h"
#  if CONFIG_ESP_CONSOLE_UART_DEFAULT
#    error \
        "FireBeetle LTE: select USB Serial/JTAG console; UART0 shares GPIO16/17 with the modem"
#  endif
#endif
#define AE_SUPPORT_WIFIS 0
#define AE_MODEM_UART_PORT "UART1"
#define AE_MODEM_UART_TX_GPIO 16
#define AE_MODEM_UART_RX_GPIO 17
#define AE_MODEM_DTR_GPIO 8
#define AE_MODEM_PWR_GPIO 14
#endif
