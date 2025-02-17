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
#include "aether/tele/declaration.h"
#include "aether/tele/env/compiler.h"
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
constexpr bool IsCompilation(TEnvConfig config) {
  return config.compiler_ || config.compilation_options_ ||
         config.library_version_ || config.api_version_;
}

template <typename TEnvConfig>
constexpr bool IsRuntime(TEnvConfig /* config */) {
  return false;
}

template <typename TEnvConfig>
constexpr bool IsAnyEnvCollection(TEnvConfig config) {
  return IsCompilation(config) || IsRuntime(config) || config.custom_data_;
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

template <typename TSink, auto Enabled = IsAnyEnvCollection(TSink::EnvConfig)>
struct EnvTele {
  using Sink = TSink;
  using EnvStream = decltype(std::declval<Sink>().trap()->env_stream());

  static constexpr auto SinkConfig = Sink::EnvConfig;

  template <typename... TValues>
  constexpr explicit EnvTele(
      Sink& sink, [[maybe_unused]] std::pair<std::size_t, TValues>&&... args) {
    auto stream = sink.trap()->env_stream();
    if constexpr (SinkConfig.platform_type_) {
      stream.platform_type(PlatformType());
    }
    if constexpr (SinkConfig.compiler_) {
      stream.compiler(CompilerName());
      stream.compiler_version(CompilerVersion());
    }
    if constexpr (SinkConfig.compilation_options_) {
      for (auto opt : CompilationOptions()) {
        stream.compilation_option(opt);
      }
    }
    if constexpr (SinkConfig.library_version_) {
      stream.library_version(LibraryVersion());
    }
    if constexpr (SinkConfig.api_version_) {
      stream.api_version(ApiVersion());
    }
    if constexpr (SinkConfig.cpu_type_) {
      stream.cpu_type(CpuType());
      stream.endianness(static_cast<uint8_t>(PlatformEndianness()));
    }

    if constexpr (SinkConfig.custom_data_) {
      (stream.custom_data(args.first, args.second), ...);
    }
  }
};

template <typename TSink>
struct EnvTele<TSink, false> {
  template <typename... TArgs>
  constexpr explicit EnvTele(TArgs&&... /* args */) {}
};
}  // namespace ae::tele

#endif  // AETHER_TELE_ENV_COLLECTORS_H_ */
