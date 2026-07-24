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

#ifndef AETHER_TELE_SINK_H_
#define AETHER_TELE_SINK_H_

#include <concepts>
#include <memory>

#include "aether/tele/itrap.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"  // IWYU pragma: keep

namespace ae::tele {
struct TeleConfig {
  bool count_metrics = true;
  bool time_metrics = true;

  bool logs_enabled = true;
  bool start_time_logs = true;
  bool level_module_logs = true;
  bool location_logs = true;
  bool name_logs = true;
  bool blob_logs = true;
};

struct EnvConfig {
  bool static_info = true;
  bool runtime_info = true;
};

template <typename T>
concept ConfigProvider = requires() {
  {
    T::template GetTeleConfig<ae::tele::Level::kDebug, 12>()
  } -> std::same_as<TeleConfig>;
  { T::GetEnvConfig() } -> std::same_as<EnvConfig>;
};

template <ConfigProvider CP>
class TeleSink {
 public:
  using ConfigProviderType = CP;

  template <Level::underlined_t l, std::uint32_t m>
  static consteval auto GetTeleConfig() {
    return ConfigProviderType::template GetTeleConfig<l, m>();
  }
  static consteval auto GetEnvConfig() {
    return ConfigProviderType::GetEnvConfig();
  }

  static TeleSink& Instance() {
    static TeleSink sink;
    return sink;
  }

  constexpr void SetTrap(std::shared_ptr<ITrap> const& trap) { trap_ = trap; }

  constexpr auto const& trap() const { return trap_; }

 private:
  std::shared_ptr<ITrap> trap_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_SINK_H_
