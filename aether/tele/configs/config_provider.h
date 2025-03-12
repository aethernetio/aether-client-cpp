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

#define _AE_LEVEL_MODULE_CONFIG(MODULE, LEVEL)                          \
  (LEVEL == ae::tele::Level::kDebug                                     \
       ? _AE_MODULE_CONFIG(MODULE, AE_TELE_DEBUG_MODULES)               \
       : (LEVEL == ae::tele::Level::kInfo                               \
              ? _AE_MODULE_CONFIG(MODULE, AE_TELE_INFO_MODULES)         \
              : (LEVEL == ae::tele::Level::kWarning                     \
                     ? _AE_MODULE_CONFIG(MODULE, AE_TELE_WARN_MODULES)  \
                     : /* ae::tele::Level::kError */ _AE_MODULE_CONFIG( \
                           MODULE, AE_TELE_ERROR_MODULES))))

struct ConfigProvider {
  template <bool count_metrics = true, bool time_metrics = true,
            bool index_logs = true, bool start_time_logs = true,
            bool level_module_logs = true, bool location_logs = true,
            bool name_logs = true, bool blob_logs = true>
  struct TeleConfig {
    static constexpr bool kCountMetrics = count_metrics;
    static constexpr bool kTimeMetrics = time_metrics;
    static constexpr bool kIndexLogs = index_logs;
    static constexpr bool kStartTimeLogs = start_time_logs;
    static constexpr bool kLevelModuleLogs = level_module_logs;
    static constexpr bool kLocationLogs = location_logs;
    static constexpr bool kNameLogs = name_logs;
    static constexpr bool kBlobLogs = blob_logs;
  };

  template <bool compiler = true, bool platform_type = true,
            bool compilation_options = true, bool library_version = true,
            bool api_version = true, bool cpu_type = true,
            bool custom_data = true>
  struct EnvConfig {
    static constexpr bool kCompiler{compiler};
    static constexpr bool kPlatformType{platform_type};
    static constexpr bool kCompilationOptions{compilation_options};
    static constexpr bool kLibraryVersion{library_version};
    static constexpr bool kApiVersion{api_version};
    static constexpr bool kCpuType{cpu_type};
    static constexpr bool kCustomData{custom_data};
  };

  template <Level::underlined_t level, std::uint32_t module>
  static constexpr auto GetStaticConfig() {
    return TeleConfig<
        //
        // count must be always enabled if metrics enabled
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_METRICS_MODULES),
        // time metrics
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_METRICS_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_METRICS_DURATION),
        // index must be always enabled if log enabled
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES),
        // start time
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_TIME_POINT),
        // level and module
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_LEVEL_MODULE),
        // location
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_LOCATION),
        // name
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_NAME),
        // blob
        _AE_LEVEL_MODULE_CONFIG(module, level) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_MODULES) &&
            _AE_MODULE_CONFIG(module, AE_TELE_LOG_BLOB)>{};
  }

  template <Level::underlined_t level, std::uint32_t module>
  using StaticTeleConfig = decltype(GetStaticConfig<level, module>());

  using StaticEnvConfig =
      EnvConfig<AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_COMPILATION_INFO != 0, AE_TELE_COMPILATION_INFO != 0,
                AE_TELE_RUNTIME_INFO>;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_CONFIGS_CONFIG_PROVIDER_H_ */
