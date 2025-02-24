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

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <utility>
#include <type_traits>

#include "aether/common.h"
#include "aether/tele/tags.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"
#include "aether/tele/declaration.h"

namespace ae::tele {
struct Timer {
  std::uint32_t elapsed() const {
    auto d = std::chrono::duration<double>{TimePoint::clock::now() - start};
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(d).count());
  }

  TimePoint const start{TimePoint::clock::now()};
};

template <typename T, bool Enabled = true>
class OptionalStorage {
 public:
  using Data = T;

  template <typename... UArgs>
  constexpr explicit OptionalStorage(UArgs&&... args)
      : data{std::forward<UArgs>(args)...} {}

  template <typename TConstructor,
            typename = std::enable_if_t<std::is_invocable_v<TConstructor>>>
  constexpr explicit OptionalStorage(TConstructor&& constructor)
      : data{constructor()} {}

  Data& operator*() noexcept { return data; }
  Data const& operator*() const noexcept { return data; }
  Data* operator->() noexcept { return &data; }
  Data const* operator->() const noexcept { return &data; }

 private:
  Data data;
};

template <typename T>
class OptionalStorage<T, false> {
 public:
  using Data = T;
  struct Null {};
  template <typename... UArgs>
  constexpr explicit OptionalStorage(UArgs&&...) {}

  Null operator*() noexcept { return {}; }
  Null operator*() const noexcept { return {}; }
  Null operator->() noexcept { return {}; }
  Null operator->() const noexcept { return {}; }
};

template <typename TConfig>
constexpr bool IsAnyLogs() {
  return TConfig::kIndexLogs || TConfig::kStartTimeLogs ||
         TConfig::kLevelModuleLogs || TConfig::kLocationLogs ||
         TConfig::kNameLogs || TConfig::kBlobLogs;
}

template <typename TConfig>
constexpr bool IsAnyMetrics() {
  return TConfig::kCountMetrics || TConfig::kTimeMetrics;
}

template <typename TConfig>
constexpr bool IsAnyTele() {
  return IsAnyLogs<TConfig>() || IsAnyMetrics<TConfig>();
}

template <typename TSink, typename TSinkConfig, typename _ = void>
struct Tele {
  // Dummy tele
  template <typename... TArgs>
  constexpr explicit Tele(TArgs&&... /* args */) {}
};

template <typename TSink, typename TSinkConfig>

struct Tele<TSink, TSinkConfig,
            std::enable_if_t<IsAnyTele<TSinkConfig>(), void>> {
  using Sink = TSink;
  using LogStream =
      decltype(std::declval<Sink>().trap()->log_stream(Declaration{}));
  using MetricStream =
      decltype(std::declval<Sink>().trap()->metric_stream(Declaration{}));

  using SinkConfig = TSinkConfig;

  constexpr Tele(Sink& sink, Tag const& tag, Level::underlined_t level,
                 char const* file, int line)
      : Tele(sink, Declaration{tag.index, tag.module}) {
    if constexpr (IsAnyLogs<SinkConfig>()) {
      auto log_stream =
          sink.trap()->log_stream(Declaration{tag.index, tag.module});
      if constexpr (SinkConfig::kIndexLogs) {
        log_stream.index(tag.index);
      }
      if constexpr (SinkConfig::kStartTimeLogs) {
        log_stream.start_time(timer_->start);
      }
      if constexpr (SinkConfig::kLevelModuleLogs) {
        log_stream.level(level);
        log_stream.module(tag.module);
      }
      if constexpr (SinkConfig::kLocationLogs) {
        log_stream.file(file);
        log_stream.line(static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig::kNameLogs) {
        log_stream.name(tag.name);
      }
    }
  }

  template <typename... TArgs>
  constexpr Tele(Sink& sink, Tag const& tag, Level::underlined_t level,
                 char const* file, int line, std::string_view format,
                 TArgs const&... args)
      : Tele(sink, Declaration{tag.index, tag.module}) {
    if constexpr (IsAnyLogs<SinkConfig>()) {
      auto log_stream =
          sink.trap()->log_stream(Declaration{tag.index, tag.module});
      if constexpr (SinkConfig::kIndexLogs) {
        log_stream.index(tag.index);
      }
      if constexpr (SinkConfig::kStartTimeLogs) {
        log_stream.start_time(timer_->start);
      }
      if constexpr (SinkConfig::kLevelModuleLogs) {
        log_stream.level(level);
        log_stream.module(tag.module);
      }
      if constexpr (SinkConfig::kLocationLogs) {
        log_stream.file(file);
        log_stream.line(static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig::kNameLogs) {
        log_stream.name(tag.name);
      }
      if constexpr (SinkConfig::kBlobLogs) {
        log_stream.blob(format, args...);
      }
    }
  }

  ~Tele() {
    if constexpr (SinkConfig::kTimeMetrics) {
      metric_stream_->add_duration(timer_->elapsed());
    }
  }

 private:
  constexpr Tele(Sink& sink, Declaration const& decl)
      : timer_{}, metric_stream_{[&sink, &decl] {
          return sink.trap()->metric_stream(decl);
        }} {
    if constexpr (SinkConfig::kCountMetrics) {
      metric_stream_->add_count(1);
    }
  }

  /** TODO:
   * think how to optimize memory for this two fields considering the
   * alignment
   */
  OptionalStorage<Timer, SinkConfig::kTimeMetrics || SinkConfig::kStartTimeLogs>
      timer_;
  OptionalStorage<MetricStream, IsAnyMetrics<SinkConfig>()> metric_stream_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_COLLECTORS_H_
