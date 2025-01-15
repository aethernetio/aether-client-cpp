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

#ifndef AETHER_TELE_COLLECTORS_H_
#define AETHER_TELE_COLLECTORS_H_

#include <chrono>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "aether/common.h"
#include "aether/tele/declaration.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"

namespace ae::tele {
struct Timer {
  std::uint32_t elapsed() const {
    auto d = std::chrono::duration<double>{TimePoint::clock::now() - start};
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
  }

  TimePoint const start{TimePoint::clock::now()};
};

template <typename T, bool Enabled = true>
class OptionalStorage {
 public:
  using Data = T;

  template <typename... UArgs>
  explicit OptionalStorage(UArgs&&... args)
      : data{std::forward<UArgs>(args)...} {}

  template <typename TConstructor,
            typename = std::enable_if_t<std::is_invocable_v<TConstructor>>>
  explicit OptionalStorage(TConstructor&& constructor) : data{constructor()} {}

  Data& operator*() noexcept { return data; }
  Data* operator->() noexcept { return &data; }

 private:
  Data data;
};

template <typename T>
class OptionalStorage<T, false> {
 public:
  using Data = T;
  struct Null {};
  template <typename... UArgs>
  explicit OptionalStorage(UArgs&&...) {}

  Null operator*() noexcept { return {}; }
  Null operator->() noexcept { return {}; }
};

template <typename TConfig>
constexpr bool IsAnyLogs(TConfig const& config) {
  return config.index_logs_ || config.start_time_logs_ ||
         config.level_module_logs_ || config.location_logs_ ||
         config.name_logs_ || config.blob_logs_;
}

template <typename TConfig>
constexpr bool IsAnyMetrics(TConfig const& config) {
  return config.count_metrics_ || config.time_metrics_;
}

template <typename TConfig>
constexpr bool IsAnyTele(TConfig config) {
  return IsAnyLogs(config) || IsAnyMetrics(config);
}

template <typename TSink, Level::underlined_t level,
          Module::underlined_t module, typename _ = void>
struct Tele {
  // Dummy tele
  template <typename... TArgs>
  explicit Tele(TArgs&&... /* args */) {}
};

template <typename TSink, Level::underlined_t level,
          Module::underlined_t module>
struct Tele<TSink, level, module,
            std::enable_if_t<
                IsAnyTele(TSink::template TeleConfig<level, module>), void>> {
  using Sink = TSink;
  using LogStream =
      decltype(std::declval<Sink>().trap()->log_stream(Declaration{}));
  using MetricStream =
      decltype(std::declval<Sink>().trap()->metric_stream(Declaration{}));

  static constexpr auto Level = level;
  static constexpr auto Module = module;
  static constexpr auto SinkConfig = Sink::template TeleConfig<Level, Module>;

  template <typename... TArgs>
  Tele(Sink& sink, int index, TArgs&&... args)
      : Tele(sink, Declaration{static_cast<std::size_t>(index), module, level},
             std::forward<TArgs>(args)...) {}

  ~Tele() {
    if constexpr (SinkConfig.time_metrics_) {
      metric_stream->add_duration(timer->elapsed());
    }
  }

 private:
  Tele(Sink& sink, Declaration decl)
      : timer{}, metric_stream{[&sink, &decl] {
          return sink.trap()->metric_stream(decl);
        }} {
    if constexpr (SinkConfig.count_metrics_) {
      metric_stream->add_count(1);
    }
  }

  Tele(Sink& sink, Declaration decl, const char* file, int line,
       const char* name)
      : Tele(sink, decl) {
    if constexpr (IsAnyLogs(SinkConfig)) {
      auto log_stream = sink.trap()->log_stream(decl);
      if constexpr (SinkConfig.index_logs_) {
        log_stream.index(decl.index_);
      }
      if constexpr (SinkConfig.start_time_logs_) {
        log_stream.start_time(timer->start);
      }
      if constexpr (SinkConfig.level_module_logs_) {
        log_stream.level(Level);
        log_stream.module(Module);
      }
      if constexpr (SinkConfig.location_logs_) {
        log_stream.file(file);
        log_stream.line(static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig.name_logs_) {
        log_stream.name(name);
      }
    }
  }

  template <typename... TArgs>
  Tele(Sink& sink, Declaration decl, const char* file, int line,
       const char* name, const char* format, TArgs&&... args)
      : Tele(sink, decl) {
    if constexpr (IsAnyLogs(SinkConfig)) {
      auto log_stream = sink.trap()->log_stream(decl);
      if constexpr (SinkConfig.index_logs_) {
        log_stream.index(decl.index_);
      }
      if constexpr (SinkConfig.start_time_logs_) {
        log_stream.start_time(timer->start);
      }
      if constexpr (SinkConfig.level_module_logs_) {
        log_stream.level(Level);
        log_stream.module(Module);
      }
      if constexpr (SinkConfig.location_logs_) {
        log_stream.file(file);
        log_stream.line(static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig.name_logs_) {
        log_stream.name(name);
      }
      if constexpr (SinkConfig.blob_logs_) {
        log_stream.blob(format, std::forward<TArgs>(args)...);
      }
    }
  }

  /** TODO:
   * think how to optimize memory for this two fields considering the
   * alignment
   */
  OptionalStorage<Timer,
                  SinkConfig.time_metrics_ || SinkConfig.start_time_logs_>
      timer;
  OptionalStorage<MetricStream, IsAnyMetrics(SinkConfig)> metric_stream;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_COLLECTORS_H_
