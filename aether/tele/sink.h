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

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <cassert>
#include <utility>

#include "aether/ptr/ptr.h"
#include "aether/tele/modules.h"
#include "aether/tele/levels.h"

namespace ae::tele {

/*
 * Expected interfaces for Metrics and Logs traps

struct Trap {
  struct MetricsStream {
    void add_count(uint32_t count);
    void add_duration(uint32_t duration);
  };

  struct LogStream {
    void index(std::size_t index);
    void start_time(TimePoint const& start);
    void level(Level::underlined_t level);
    void module(Module::underlined_t module);
    void file(char const* file);
    void line(std::uint32_t line);
    void name(char const* name);

    template <typename... TArgs>
    void blob(char const* format, TArgs const&... args);
  };

  MetricsStream metric_stream(Declaration decl_);
  LogStream log_stream(Declaration decl_);
};

struct ConfigProvider
{
  struct Config {
    bool CountMetrics = true;
    bool TimeMetrics = true;
    bool IndexLogs = true;
    bool StartTimeLogs = true;
    bool LevelModuleLogs = true;
    bool LocationLogs = true;
    bool TextLogs = true;
    bool MessageLogs = true;
    bool BlobLogs = true;

    bool Any =
        CountMetrics || TimeMetrics || IndexLogs || StartTimeLogs ||
        LevelModuleLogs || LocationLogs || TextLogs || MessageLogs || BlobLogs;
  };

  template<Level::underlined_t level, Module::underlined_t module>
  static constexpr auto StaticConfig = Config{};
};
*/

template <typename TTrap, typename ConfigProvider>
class TeleSink {
 public:
  using TrapType = TTrap;
  using ConfigProviderType = ConfigProvider;

  template <Level::underlined_t l, std::uint32_t m>
  static constexpr auto TeleConfig =
      ConfigProviderType::template StaticTeleConfig<l, m>;

  static constexpr auto EnvConfig = ConfigProviderType::StaticEnvConfig;

  static TeleSink& Instance() {
    static TeleSink sink;
    return sink;
  }

  static void InitSink(Ptr<TrapType> trap) {
    Instance().trap_ = std::move(trap);
  }

  auto* trap() {
    assert(trap_);
    return trap_.get();
  }

 private:
  Ptr<TrapType> trap_{};
};
}  // namespace ae::tele

#endif  // AETHER_TELE_SINK_H_
