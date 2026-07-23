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
#include "aether/tele/sink.h"

namespace ae::tele {
struct AllEnabledConfig {
  template <Level::underlined_t, std::uint32_t>
  static consteval auto GetTeleConfig() {
    return TeleConfig{
        .count_metrics = true,
        .time_metrics = true,
        .logs_enabled = true,
        .start_time_logs = true,
        .level_module_logs = true,
        .location_logs = true,
        .name_logs = true,
        .blob_logs = true,
    };
  }
  static consteval auto GetEnvConfig() {
    return EnvConfig{
        .static_info = true,
        .runtime_info = true,
    };
  }
};
}  // namespace ae::tele

#endif  // AETHER_TELE_CONFIGS_ALL_ENABLED_H_
