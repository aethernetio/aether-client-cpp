/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef EXAMPLES_A_B_MESSAGE_EXCHANGE_USER_CONFIG_H_
#define EXAMPLES_A_B_MESSAGE_EXCHANGE_USER_CONFIG_H_

#include "aether/config_consts.h"

/**
 * \brief For full config list and default values \see aether/config.h
 *
 * Desktop ping-pong (ab-message-exchange): force TCP-only cloud/server
 * channels. Without this, connectionless UDP channels sort first and are
 * preferred over TCP.
 */

#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_SIGNATURE AE_HYDRO_SIGNATURE
#define AE_KDF AE_HYDRO_KDF

#define AE_SUPPORT_UDP 0
#define AE_SUPPORT_TCP 1

#if !ESP_PLATFORM
#  define AE_SUPPORT_WIFIS 0
#endif

#define AE_TELE_ENABLED 1
#define AE_TELE_LOG_CONSOLE 1
#define AE_TELE_COMPILATION_INFO 1
#define AE_TELE_LOG_TO_STATISTICS 1
#define AE_STATISTICS_MAX_SIZE 1024

#define AE_TELE_METRICS_MODULES_EXCLUDE \
  { AE_LOG_MODULE }
#define AE_TELE_METRICS_DURATION_EXCLUDE \
  { AE_LOG_MODULE }

#define AE_TELE_LOG_MODULES AE_ALL
#define AE_TELE_DEBUG_MODULES AE_ALL
#define AE_TELE_INFO_MODULES AE_ALL
#define AE_TELE_WARN_MODULES AE_ALL
#define AE_TELE_ERROR_MODULES AE_ALL

#define AE_TELE_LOG_TIME_POINT AE_ALL
#define AE_TELE_LOG_LOCATION \
  { AE_LOG_MODULE }
#define AE_TELE_LOG_NAME_EXCLUDE \
  { AE_LOG_MODULE }
#define AE_TELE_LOG_LEVEL_MODULE AE_ALL
#define AE_TELE_LOG_BLOB AE_ALL

#if AE_DISTILLATION || AE_FILTRATION
#  define AE_SUPPORT_REGISTRATION 1
#  define AE_SUPPORT_CLOUD_DNS 1
#else
#  define AE_SUPPORT_REGISTRATION 0
#  define AE_SUPPORT_CLOUD_DNS 0
#endif

#endif  // EXAMPLES_A_B_MESSAGE_EXCHANGE_USER_CONFIG_H_
