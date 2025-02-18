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

#ifndef AETHER_TELE_CONFIGS_CONFIG_PROVIDER_H_
#define AETHER_TELE_CONFIGS_CONFIG_PROVIDER_H_

#include <cstdint>

#include "aether/config.h"

#include "aether/tele/levels.h"

namespace ae::tele {
template <typename T>
constexpr bool ContainsFlag(std::uint32_t value, T flag) {
  return (value & static_cast<std::uint32_t>(flag)) ==
         static_cast<std::uint32_t>(flag);
}

template <auto flag>
constexpr bool IsFlagEnabled(std::uint32_t const value) {
  return AE_TELE_ENABLED && ContainsFlag(value, flag);
}

template <auto flag>
constexpr bool IsEnabled(std::initializer_list<decltype(flag)> list) {
  for (auto v : list) {
    if (flag == v) {
      return AE_TELE_ENABLED;
    }
  }
  return false;
}

template <auto flag>
constexpr bool IsEnabled(decltype(flag) value) {
  return AE_TELE_ENABLED && (flag == value);
}

template <typename T>
constexpr bool IsAll(T const value) {
  return AE_TELE_ENABLED && value == static_cast<T>(AE_ALL);
}
template <typename T, std::size_t Size>
constexpr bool IsAll(T const (& /* value */)[Size]) {
  return false;
}

// OPTION equals to AE_ALL or MODULE in OPTION list and not in OPTION_EXCLUDE
// list
#define _AE_MODULE_CONFIG(MODULE, OPTION)                      \
  (((IsAll(OPTION) && !IsEnabled<MODULE>(OPTION##_EXCLUDE)) || \
    IsEnabled<MODULE>(OPTION)))

struct ConfigProvider {
  struct TeleConfig {
    bool count_metrics_{};
    bool time_metrics_{};
    bool index_logs_{};
    bool start_time_logs_{};
    bool level_module_logs_{};
    bool location_logs_{};
    bool name_logs_{};
    bool blob_logs_{};
  };

  struct EnvConfig {
    bool compiler_{};
    bool platform_type_{};
    bool compilation_options_{};
    bool library_version_{};
    bool api_version_{};
    bool cpu_type_{};
    bool custom_data_{};
  };

  template <Level::underlined_t level, std::uint32_t module>
  static constexpr auto GetStaticConfig() {
    return TeleConfig{// count must be always enabled if metrics enabled
                      _AE_MODULE_CONFIG(module, AE_TELE_METRICS_MODULES),
                      // time metrics
                      _AE_MODULE_CONFIG(module, AE_TELE_METRICS_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_METRICS_DURATION),
                      // index must be always enabled if log enabled
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES),
                      // start time
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_TIME_POINT),
                      // level and module
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_LEVEL_MODULE),
                      // location
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_LOCATION),
                      // name
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_NAME),
                      // blob
                      IsFlagEnabled<level>(AE_TELE_LOG_LEVELS) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
                          _AE_MODULE_CONFIG(module, AE_TELE_LOG_BLOB)};
  }

  template <Level::underlined_t level, std::uint32_t module>
  static constexpr TeleConfig StaticTeleConfig =
      GetStaticConfig<level, module>();

  static constexpr EnvConfig StaticEnvConfig =
      EnvConfig{AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_RUNTIME_INFO};
};
}  // namespace ae::tele

#endif  // AETHER_TELE_CONFIGS_CONFIG_PROVIDER_H_ */
