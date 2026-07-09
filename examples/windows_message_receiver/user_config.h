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

#ifndef USER_CONFIG_H_
#define USER_CONFIG_H_

#include "aether/config_consts.h"
/**
 * \brief For full config list and default values \see aether/config.h
 */

// use hydrogen encryption
#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_SIGNATURE AE_HYDRO_SIGNATURE
#define AE_KDF AE_HYDRO_KDF

// disable debug telemetry on release
#define AE_TELE_ENABLED 1
#define AE_TELE_LOG_CONSOLE 1
#if defined NDEBUG
#  define AE_TELE_DEBUG_MODULES 0
#else
#  define AE_TELE_DEBUG_MODULES AE_ALL
#endif
#endif  // USER_CONFIG_H_
