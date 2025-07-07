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

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <array>
#include <cstdint>
#include <utility>

#include "aether/env.h"
#include "aether/tele/itrap.h"
#include "aether/tele/env/compiler.h"
#include "aether/tele/compile_option.h"
#include "aether/tele/env/platform_type.h"
#include "aether/tele/env/library_version.h"
#include "aether/tele/env/cpu_architecture.h"
#include "aether/tele/env/compilation_options.h"

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

template <typename TEnvConfig>
constexpr bool IsCompilation() {
  return TEnvConfig::kCompiler || TEnvConfig::kCompilationOptions ||
         TEnvConfig::kLibraryVersion || TEnvConfig::kApiVersion;
}

template <typename TEnvConfig>
constexpr bool IsRuntime() {
  return false;
}

template <typename TEnvConfig>
constexpr bool IsAnyEnvCollection() {
  return IsCompilation<TEnvConfig>() || IsRuntime<TEnvConfig>() ||
         TEnvConfig::kCustomData;
}

constexpr auto PlatformType() { return AE_PLATFORM_TYPE; }
constexpr auto CompilerName() { return COMPILER; }
constexpr auto CompilerVersion() { return COMPILER_VERSION; }

template <typename OptsArr, size_t... Is>
constexpr auto CompilationOptionsImpl(OptsArr const& arr,
                                      std::index_sequence<Is...>) {
  return std::array<CompileOption, sizeof...(Is)>{
      CompileOption{Is, arr[Is].first, arr[Is].second}...};
}
constexpr auto CompilationOptions() {
  return CompilationOptionsImpl(
      _compile_options_list,
      std::make_index_sequence<_compile_options_list.size()>{});
}
constexpr auto LibraryVersion() { return LIBRARY_VERSION; }
constexpr auto ApiVersion() {
  // TODO: add collect api version
  return "0.0.0";
}

constexpr auto PlatformEndianness() { return AE_ENDIANNESS; }
constexpr auto CpuType() { return AE_CPU_TYPE; }

template <typename TSink,
          auto Enabled = IsAnyEnvCollection<typename TSink::EnvConfig>()>
struct EnvTele {
  using Sink = TSink;
  using SinkConfig = typename Sink::EnvConfig;

  template <typename... TValues>
  explicit EnvTele(
      Sink& sink, [[maybe_unused]] std::uint32_t utm_id,
      [[maybe_unused]] std::pair<std::string_view, TValues>&&... args) {
    auto env_data = EnvData{};
    if constexpr (SinkConfig::kPlatformType) {
      env_data.platform_type = PlatformType();
    }
    if constexpr (SinkConfig::kCompiler) {
      env_data.compiler = CompilerName();
      env_data.compiler_version = CompilerVersion();
    }
    if constexpr (SinkConfig::kCompilationOptions) {
      auto opts = CompilationOptions();
      env_data.compile_options.insert(std::end(env_data.compile_options),
                                      std::begin(opts), std::end(opts));
    }
    if constexpr (SinkConfig::kLibraryVersion) {
      env_data.library_version = LibraryVersion();
    }
    if constexpr (SinkConfig::kApiVersion) {
      env_data.api_version = ApiVersion();
    }
    if constexpr (SinkConfig::kCpuType) {
      env_data.cpu_arch = CpuType();
      env_data.endianness = static_cast<std::uint8_t>(PlatformEndianness());
    }
    if constexpr (SinkConfig::kUtmId) {
      env_data.utm_id = utm_id;
    }
    if constexpr (SinkConfig::kCustomData) {
      env_data.custom_options = {CustomOption{args.first, args.second}...};
    }
    auto& trap = sink.trap();
    if (!trap) {
      return;
    }
    trap->WriteEnvData(env_data);
  }
};

template <typename TSink>
struct EnvTele<TSink, false> {
  template <typename... TArgs>
  constexpr explicit EnvTele(TArgs&&... /* args */) {}
};
}  // namespace ae::tele

#endif  // AETHER_TELE_ENV_COLLECTORS_H_ */
