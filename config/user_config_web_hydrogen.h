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

#ifndef CONFIG_USER_CONFIG_WEB_HYDROGEN_H_
#define CONFIG_USER_CONFIG_WEB_HYDROGEN_H_
#include "config/user_config_hydrogen.h"
// The browser resolves names and validates WSS certificates itself. No native
// TCP/UDP proxy or c-ares dependency is needed.
#undef AE_SUPPORT_CLOUD_DNS
#define AE_SUPPORT_CLOUD_DNS 0
#define AE_SUPPORT_TCP 0
#define AE_SUPPORT_UDP 0
#define AE_SUPPORT_HTTP 0
#endif
