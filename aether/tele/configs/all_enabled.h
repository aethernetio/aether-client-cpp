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

#ifndef AETHER_TELE_CONFIGS_ALL_ENABLED_H_
#define AETHER_TELE_CONFIGS_ALL_ENABLED_H_

#include <cstdint>

#include "aether/tele/levels.h"

namespace ae::tele {
struct AllEnabledConfig {
  template <Level::underlined_t, std::uint32_t>
  struct TeleConfig {
    static constexpr bool kCountMetrics = true;
    static constexpr bool kTimeMetrics = true;
    static constexpr bool kLogsEnabled = true;
    static constexpr bool kStartTimeLogs = true;
    static constexpr bool kLevelModuleLogs = true;
    static constexpr bool kLocationLogs = true;
    static constexpr bool kNameLogs = true;
    static constexpr bool kBlobLogs = true;
  };

  struct EnvConfig {
    static constexpr bool kStaticInfo = true;
    static constexpr bool kRuntimeInfo = true;
  };
};
}  // namespace ae::tele

#endif  // AETHER_TELE_CONFIGS_ALL_ENABLED_H_
