/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef CONFIG_USER_CONFIG_HYDROGEN_H_
#define CONFIG_USER_CONFIG_HYDROGEN_H_

#include "aether/config_consts.h"

#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_SIGNATURE AE_HYDRO_SIGNATURE
#define AE_KDF AE_HYDRO_KDF

// telemetry
#define AE_TELE_ENABLED 1
#define AE_TELE_LOG_CONSOLE 1

// all except MLog
#define AE_TELE_METRICS_MODULES_EXCLUDE {AE_LOG_MODULE}
#define AE_TELE_METRICS_DURATION_EXCLUDE {AE_LOG_MODULE}

#define AE_TELE_LOG_MODULES AE_ALL
#define AE_TELE_LOG_LEVELS AE_TELE_LEVELS_ALL

#define AE_TELE_LOG_TIME_POINT AE_ALL
// location only for kLog module
#define AE_TELE_LOG_LOCATION {AE_LOG_MODULE}
// tag name for all except kLog
#define AE_TELE_LOG_NAME_EXCLUDE {AE_LOG_MODULE}
#define AE_TELE_LOG_LEVEL_MODULE AE_ALL
#define AE_TELE_LOG_BLOB AE_ALL

#if defined AE_DISTILLATION
#  define AE_SUPPORT_REGISTRATION 1
#  define AE_SUPPORT_CLOUD_DNS 1
#else
#  define AE_SUPPORT_REGISTRATION 0
#  define AE_SUPPORT_CLOUD_DNS 0
#endif

#endif /* CONFIG_USER_CONFIG_HYDROGEN_H_ */
