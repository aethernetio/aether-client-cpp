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

#ifndef AETHER_AETHER_TELE_H_
#define AETHER_AETHER_TELE_H_

#include <cstdint>

#include "aether/config.h"
#include "aether/tele/levels.h"
#include "aether/tele/tags.h"

namespace ae {
namespace aether_tele_internal {
template <auto flag>
consteval bool IsEnabled(std::initializer_list<decltype(flag)> list) {
  for (auto v : list) {
    if (flag == v) {
      return true;
    }
  }
  return false;
}

template <auto flag>
consteval bool IsEnabled(decltype(flag) value) {
  return (flag == value);
}

template <typename T>
consteval bool IsAll(T const value) {
  return value == static_cast<T>(AE_ALL);
}
template <typename T, std::size_t Size>
consteval bool IsAll(T const (& /* value */)[Size]) {  // NOLINT(*c-arrays)
  return false;
}
}  // namespace aether_tele_internal

// NOLINTBEGIN
// OPTION equals to AE_ALL or MODULE in OPTION list and not in OPTION_EXCLUDE
// list
#define _AE_MODULE_CONFIG(MODULE, OPTION)                           \
  (((aether_tele_internal::IsAll(OPTION) &&                         \
     !aether_tele_internal::IsEnabled<MODULE>(OPTION##_EXCLUDE)) || \
    aether_tele_internal::IsEnabled<MODULE>(OPTION)))

#define _AE_MODULE_DEBUG_ENABLED(MODULE) \
  _AE_MODULE_CONFIG(MODULE, AE_TELE_DEBUG_MODULES)
#define _AE_MODULE_INFO_ENABLED(MODULE) \
  _AE_MODULE_CONFIG(MODULE, AE_TELE_INFO_MODULES)
#define _AE_MODULE_WARN_ENABLED(MODULE) \
  _AE_MODULE_CONFIG(MODULE, AE_TELE_WARN_MODULES)
#define _AE_MODULE_ERROR_ENABLED(MODULE) \
  _AE_MODULE_CONFIG(MODULE, AE_TELE_ERROR_MODULES)

// NOLINTNEXTLINE
#define _AE_LEVEL_MODULE_CONFIG(MODULE, LEVEL)                                \
  ((LEVEL) == ae::tele::Level::kDebug && _AE_MODULE_DEBUG_ENABLED(MODULE)) || \
      ((LEVEL) == ae::tele::Level::kInfo &&                                   \
       _AE_MODULE_INFO_ENABLED(MODULE)) ||                                    \
      ((LEVEL) == ae::tele::Level::kWarning &&                                \
       _AE_MODULE_WARN_ENABLED(MODULE)) ||                                    \
      ((LEVEL) == ae::tele::Level::kError && _AE_MODULE_ERROR_ENABLED(MODULE))

struct AetherTeleConfig {
  static constexpr bool kGlobalTeleEnabled = AE_TELE_ENABLED;

  template <tele::Level::underlined_t L, std::uint32_t M>
  struct TeleConfig {
    // IF the whole Level + Module enabled
    static constexpr bool kTeleEnabled =
        kGlobalTeleEnabled && _AE_LEVEL_MODULE_CONFIG(M, L);

    static constexpr bool kLogsEnabled =
        kTeleEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_MODULES);

    // count must be always enabled if metrics enabled
    static constexpr bool kCountMetrics =
        kTeleEnabled && _AE_MODULE_CONFIG(M, AE_TELE_METRICS_MODULES);
    // time metrics
    static constexpr bool kTimeMetrics =
        kTeleEnabled && _AE_MODULE_CONFIG(M, AE_TELE_METRICS_MODULES) &&
        _AE_MODULE_CONFIG(M, AE_TELE_METRICS_DURATION);

    // start time
    static constexpr bool kStartTimeLogs =
        kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_TIME_POINT);
    // level and module
    static constexpr bool kLevelModuleLogs =
        kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_LEVEL_MODULE);
    // location
    static constexpr bool kLocationLogs =
        kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_LOCATION);
    // name
    static constexpr bool kNameLogs =
        kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_NAME);
    // blob
    static constexpr bool kBlobLogs =
        kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_BLOB);
  };

  struct EnvConfig {
    static constexpr bool kStaticInfo = AE_TELE_COMPILATION_INFO;
    static constexpr bool kRuntimeInfo = AE_TELE_RUNTIME_INFO;
  };
};
}  // namespace ae

// Main module for aether and tags
AE_TELE_MODULE(kAether, 2, 3, 5);
AE_TAG(AetherStarted, kAether)
AE_TAG(AetherCreated, kAether)
AE_TAG(AetherDestroyed, kAether)

#endif  // AETHER_AETHER_TELE_H_
