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
#include "aether/tele/sink.h"
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
#define _AE_MODULE_CONFIG(MODULE, OPTION)                                 \
  (((::ae::aether_tele_internal::IsAll(OPTION) &&                         \
     !::ae::aether_tele_internal::IsEnabled<MODULE>(OPTION##_EXCLUDE)) || \
    ::ae::aether_tele_internal::IsEnabled<MODULE>(OPTION)))

// NOLINTEND

namespace aether_tele_internal {
template <auto M>
consteval bool AeModuleDebugEnabled() {
  return _AE_MODULE_CONFIG(M, AE_TELE_DEBUG_MODULES);
}
template <auto M>
consteval bool AeModuleInfoEnabled() {
  return _AE_MODULE_CONFIG(M, AE_TELE_INFO_MODULES);
}
template <auto M>
consteval bool AeModuleWarnEnabled() {
  return _AE_MODULE_CONFIG(M, AE_TELE_WARN_MODULES);
}
template <auto M>
consteval bool AeModuleErrorEnabled() {
  return _AE_MODULE_CONFIG(M, AE_TELE_ERROR_MODULES);
}

template <tele::Level::underlined_t L, std::uint32_t M>
consteval bool LevelModuleConfig() {
  return ((L) == ae::tele::Level::kDebug && AeModuleDebugEnabled<M>()) ||
         ((L) == ae::tele::Level::kInfo && AeModuleInfoEnabled<M>()) ||
         ((L) == ae::tele::Level::kWarning && AeModuleWarnEnabled<M>()) ||
         ((L) == ae::tele::Level::kError && AeModuleErrorEnabled<M>());
}
}  // namespace aether_tele_internal

struct AetherTeleConfig {
  static constexpr bool kGlobalTeleEnabled = AE_TELE_ENABLED;

  template <tele::Level::underlined_t L, std::uint32_t M>
  static consteval auto GetTeleConfig() {
    // IF the whole Level + Module enabled
    // cppcheck-suppress-begin *
    constexpr bool kTeleEnabled =
        kGlobalTeleEnabled && aether_tele_internal::LevelModuleConfig<L, M>();
    // cppcheck-suppress-end *

    constexpr bool kMetricsEnabled =
        kTeleEnabled && _AE_MODULE_CONFIG(M, AE_TELE_METRICS_MODULES);
    constexpr bool kLogsEnabled =
        kTeleEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_MODULES);

    return tele::TeleConfig{
        .count_metrics = kMetricsEnabled,
        .time_metrics =
            kMetricsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_METRICS_DURATION),

        .logs_enabled = kLogsEnabled,
        .start_time_logs =
            kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_TIME_POINT),
        .level_module_logs =
            kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_LEVEL_MODULE),
        .location_logs =
            kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_LOCATION),
        .name_logs = kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_NAME),
        .blob_logs = kLogsEnabled && _AE_MODULE_CONFIG(M, AE_TELE_LOG_BLOB),
    };
  }

  static consteval auto GetEnvConfig() {
    return tele::EnvConfig{
        .static_info = AE_TELE_COMPILATION_INFO,
        .runtime_info = AE_TELE_RUNTIME_INFO,
    };
  };
};
}  // namespace ae

// Main module for aether and tags
AE_TELE_MODULE(kAether, 2, 3, 5);
AE_TAG(AetherStarted, kAether)
AE_TAG(AetherCreated, kAether)
AE_TAG(AetherDestroyed, kAether)

#endif  // AETHER_AETHER_TELE_H_
