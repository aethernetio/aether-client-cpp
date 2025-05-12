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

#ifndef AETHER_TELE_ENV_COMPILATION_OPTIONS_H_
#define AETHER_TELE_ENV_COMPILATION_OPTIONS_H_

#include <array>
#include <cstdlib>
#include <cstdint>
#include <utility>
#include <string_view>

#include "aether/config.h"
#include "aether/common.h"

#define _OPTION_VALUE(option, value)                           \
  std::pair {                                                  \
    std::string_view{#option}, std::string_view { STR(value) } \
  }

#define _OPTION(option) \
  std::pair { std::string_view{#option}, std::string_view{VA_STR(option)}, }

namespace ae {
template <std::size_t N>
constexpr auto str_to_ui64(char const (&str)[N]) {
  std::uint64_t result = 0;
  std::uint64_t base = 10;
  std::size_t i = 0;

  if constexpr (N > 2) {
    if (N > 3 && str[0] == '0' && str[1] == 'x') {
      base = 16;
      i = 2;
    } else if (str[0] == '0') {
      base = 8;
      i = 1;
    } else if (str[0] == 'b') {
      base = 2;
      i = 1;
    }
  }

  for (; i < N; ++i) {
    if ((str[i] >= '0') && (str[i] <= '9')) {
      result = result * base + static_cast<uint64_t>(str[i] - '0');
    }
    if ((base > 10) && (str[i] >= 'a') && (str[i] <= 'f')) {
      result = result * base + static_cast<uint64_t>(str[i] - 'a' + 10);
    }
    if ((base > 10) && (str[i] >= 'A') && (str[i] <= 'F')) {
      result = result * base + static_cast<uint64_t>(str[i] - 'A' + 10);
    }
  }
  return result;
}

// some tests
static_assert(str_to_ui64("0") == 0);
static_assert(str_to_ui64("1") == 1);
static_assert(str_to_ui64("112") == 112);
static_assert(str_to_ui64("0xffff") == 65535);
static_assert(str_to_ui64("0xAaCb") == 0xaacb);
static_assert(str_to_ui64("042") == 34);
static_assert(str_to_ui64("b110") == 6);

constexpr inline auto _compile_options_list = std::array{
    _OPTION(AE_SUPPORT_IPV4),
    _OPTION(AE_SUPPORT_IPV6),
    _OPTION(AE_SUPPORT_UDP),
    _OPTION(AE_SUPPORT_TCP),
    _OPTION(AE_SUPPORT_WEBSOCKET),
    _OPTION(AE_SUPPORT_HTTP),
    _OPTION(AE_SUPPORT_HTTP_OVER_TCP),
    _OPTION(AE_SUPPORT_HTTP_WINHTTP),
    _OPTION(AE_SUPPORT_HTTPS),
    _OPTION(AE_SUPPORT_PROXY),
    _OPTION(AE_SUPPORT_DYNAMIC_PROXY),
    _OPTION(AE_SUPPORT_CLOUD_DNS),
    _OPTION(AE_SUPPORT_DYNAMIC_CLOUD_DNS),
    _OPTION(AE_SUPPORT_CLOUD_IPS),
    _OPTION(AE_SUPPORT_DYNAMIC_CLOUD_IPS),
    _OPTION(AE_SUPPORT_REGISTRATION),
    _OPTION(AE_SUPPORT_REGISTRATION_DYNAMIC_IP),
    _OPTION(AE_PING_INTERVAL_MS),
    _OPTION(AE_BCRYPT_CRC32),
    _OPTION(AE_POW),
    _OPTION(AE_SIGNATURE),
    _OPTION(AE_CRYPTO_ASYNC),
    _OPTION(AE_CRYPTO_SYNC),
    _OPTION(AE_KDF),
    _OPTION(AE_CRYPTO_HASH),
    _OPTION(AE_TARGET_ENDIANNESS),
    _OPTION(AE_SUPPORT_SPIFS_V1_FS),
    _OPTION(AE_SUPPORT_SPIFS_V2_FS),
    _OPTION(AE_SAFE_STREAM_RTO_GROW_FACTOR),
    _OPTION(AE_STATISTICS_CONNECTION_WINDOW_SIZE),
    _OPTION(AE_STATISTICS_PING_WINDOW_SIZE),
    _OPTION(AE_TELE_ENABLED),
    _OPTION(AE_TELE_COMPILATION_INFO),
    _OPTION(AE_TELE_RUNTIME_INFO),
    _OPTION(AE_TELE_METRICS_MODULES),
    _OPTION(AE_TELE_METRICS_MODULES_EXCLUDE),
    _OPTION(AE_TELE_METRICS_DURATION),
    _OPTION(AE_TELE_METRICS_DURATION_EXCLUDE),
    _OPTION(AE_TELE_DEBUG_MODULES),
    _OPTION(AE_TELE_DEBUG_MODULES_EXCLUDE),
    _OPTION(AE_TELE_INFO_MODULES),
    _OPTION(AE_TELE_INFO_MODULES_EXCLUDE),
    _OPTION(AE_TELE_WARN_MODULES),
    _OPTION(AE_TELE_WARN_MODULES_EXCLUDE),
    _OPTION(AE_TELE_ERROR_MODULES),
    _OPTION(AE_TELE_ERROR_MODULES_EXCLUDE),
    _OPTION(AE_TELE_LOG_MODULES),
    _OPTION(AE_TELE_LOG_MODULES_EXCLUDE),
    _OPTION(AE_TELE_LOG_LOCATION),
    _OPTION(AE_TELE_LOG_LOCATION_EXCLUDE),
    _OPTION(AE_TELE_LOG_NAME),
    _OPTION(AE_TELE_LOG_NAME_EXCLUDE),
    _OPTION(AE_TELE_LOG_LEVEL_MODULE),
    _OPTION(AE_TELE_LOG_LEVEL_MODULE_EXCLUDE),
    _OPTION(AE_TELE_LOG_BLOB),
    _OPTION(AE_TELE_LOG_BLOB_EXCLUDE),
    _OPTION(AE_TELE_LOG_CONSOLE),
#if defined AE_DISTILLATION
    _OPTION_VALUE(AE_DISTILLATION, 1),
#else
    _OPTION_VALUE(AE_DISTILLATION, 0),
#endif
#if defined DEBUG
    _OPTION_VALUE(DEBUG, 1),
#else
    _OPTION_VALUE(DEBUG, 0),
#endif
};
}  // namespace ae
#endif  // AETHER_TELE_ENV_COMPILATION_OPTIONS_H_ */
