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

#ifndef CONFIG_USER_CONFIG_ANDROID_SMOKE_H_
#define CONFIG_USER_CONFIG_ANDROID_SMOKE_H_

#include "aether/config_consts.h"

// Minimal crypto for local AetherApp smoke (not a production default).
#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_SIGNATURE AE_HYDRO_SIGNATURE
#define AE_KDF AE_HYDRO_KDF

#define AE_SUPPORT_REGISTRATION 0
#define AE_SUPPORT_CLOUD_DNS 0
#define AE_SUPPORT_HTTP 0
#define AE_SUPPORT_HTTPS 0
#define AE_SUPPORT_PROXY 0
#define AE_SUPPORT_WIFIS 0
#define AE_SUPPORT_MODEMS 0
#define AE_SUPPORT_LORA 0
#define AE_SUPPORT_GATEWAY 0

#define AE_TELE_ENABLED 0
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0

#define AE_ENABLE_PING 0

#endif /* CONFIG_USER_CONFIG_ANDROID_SMOKE_H_ */
