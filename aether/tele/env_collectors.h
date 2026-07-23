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

#ifndef AETHER_TELE_ENV_COLLECTORS_H_
#define AETHER_TELE_ENV_COLLECTORS_H_

#include <array>
#include <cstdint>
#include <utility>

#include "aether/env.h"
#include "aether/tele/compile_option.h"
#include "aether/tele/env/compiler.h"
#include "aether/tele/env/cpu_architecture.h"
#include "aether/tele/env/library_version.h"
#include "aether/tele/env/platform_type.h"
#include "aether/tele/itrap.h"
#include "aether/tele/sink.h"

namespace ae::tele {
/**
 * Collect environment information about application
 *  - Build data
 *     - compiler
 *     - compiler version
 *     - compilation options (enabled crypto flags, enabled telemetry, debug and
 * distillation modes)
 *     - library version (git hash and git origin remote url)
 *     - API version
 *  - Target platform info
 *     - OS type
 *     - chip type
 *     - architecture
 */

constexpr auto PlatformType() { return AE_PLATFORM_TYPE; }
constexpr auto CompilerName() { return COMPILER; }
constexpr auto CompilerVersion() { return COMPILER_VERSION; }
constexpr auto LibraryVersion() { return LIBRARY_VERSION; }

constexpr Endianness PlatformEndianness() {
  return static_cast<Endianness>(AE_ENDIANNESS);
}
constexpr auto CpuType() { return AE_CPU_TYPE; }

template <typename TSink, EnvConfig Config, typename Enabled = void>
struct EnvTele {
  template <typename... TArgs>
  constexpr explicit EnvTele(TArgs&&... /* args */) {}
};

template <typename TSink, EnvConfig Config>
struct EnvTele<TSink, Config,
               std::enable_if_t<Config.static_info || Config.runtime_info>> {
  using Sink = TSink;
  static constexpr auto SinkConfig = Config;

  template <std::size_t CompileConfigs = 0, std::size_t CustomOptions = 0>
  explicit EnvTele(
      Sink& sink, [[maybe_unused]] std::uint32_t utm_id,
      [[maybe_unused]] std::array<CompileOption, CompileConfigs> const&
          compile_options = {},
      [[maybe_unused]] std::array<CustomOption, CustomOptions> const&
          custom_options = {}) {
    auto env_data = EnvData{};

    if constexpr (SinkConfig.static_info) {
      env_data.platform_type = PlatformType();
      env_data.compiler = CompilerName();
      env_data.compiler_version = CompilerVersion();
      env_data.library_version = LibraryVersion();
      env_data.cpu_arch = CpuType();
      env_data.endianness = PlatformEndianness();
      env_data.utm_id = utm_id;
      env_data.compile_options = std::span{compile_options};
    }
    if constexpr (SinkConfig.runtime_info) {
      env_data.custom_options = std::span{custom_options};
    }
    auto& trap = sink.trap();
    if (!trap) {
      return;
    }
    trap->WriteEnvData(env_data);
  }
};

}  // namespace ae::tele

#endif  // AETHER_TELE_ENV_COLLECTORS_H_ */
