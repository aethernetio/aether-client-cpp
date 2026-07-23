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
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "aether-miscpp/format/format.h"

#include "aether/tele/itrap.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"  // IWYU pragma: keep
#include "aether/tele/tags.h"

namespace ae::tele {
namespace collectors_internal {
struct EmptyNull {
  template <typename... Args>
  constexpr explicit EmptyNull(Args&&...) noexcept {}
};

template <bool condition, typename TrueType>
struct OrEmpty {
  using type = std::conditional_t<condition, TrueType, EmptyNull>;
};

static constexpr inline auto kEmptyFormat = FormatScheme{""};
}  // namespace collectors_internal

template <bool condition, typename TrueType>
using OrEmpty_t =
    typename collectors_internal::OrEmpty<condition, TrueType>::type;

struct Timer {
  Duration elapsed() const {
    auto d = TimePoint::clock::now() - start;
    return std::chrono::duration_cast<Duration>(d);
  }

  TimePoint const start{TimePoint::clock::now()};
};

struct TimedTele {
  Timer timer;
  Tag const& tag;
  std::shared_ptr<ITrap> trap;
};

template <typename TConfig>
static constexpr inline bool kIsAnyLogs = TConfig::kLogsEnabled;

template <typename TConfig>
static constexpr inline bool kIsAnyMetrics =
    TConfig::kCountMetrics || TConfig::kTimeMetrics;

template <typename TConfig>
static constexpr inline bool kIsAnyTele =
    kIsAnyLogs<TConfig> || kIsAnyMetrics<TConfig>;

template <typename TSinkConfig, typename Enabled = void>
struct TeleLogCollector;

// Specialization for disabled Logs
template <typename TSinkConfig>
struct TeleLogCollector<TSinkConfig, std::enable_if_t<!kIsAnyLogs<TSinkConfig>>>
    final : public ILogCollector {
  template <typename... TArgs>
  constexpr explicit TeleLogCollector(TArgs&&...) noexcept {}

  void WriteLine(ILogLine&) override {}
};

// Specialization for enabled Logs
template <typename TSinkConfig>
struct TeleLogCollector<TSinkConfig, std::enable_if_t<kIsAnyLogs<TSinkConfig>>>
    final : public ILogCollector {
  using SinkConfig = TSinkConfig;

  template <typename... Args>
  constexpr explicit TeleLogCollector(Tag const& t, Level l, std::string_view f,
                                      std::uint32_t f_l,
                                      std::span<std::uint8_t const> b) noexcept
      : tag{t}, level{l}, file{f}, line{f_l}, blob{b} {}

  void WriteLine([[maybe_unused]] ILogLine& log_line) override {
    if constexpr (SinkConfig::kStartTimeLogs) {
      log_line.InvokeTime(TimePoint::clock::now());
    }
    if constexpr (SinkConfig::kLevelModuleLogs) {
      log_line.WriteLevel(level);
      log_line.WriteModule(tag.module);
    }
    if constexpr (SinkConfig::kLocationLogs) {
      log_line.Location(file, line);
    }
    if constexpr (SinkConfig::kNameLogs) {
      log_line.TagName(tag.name);
    }
    if constexpr (SinkConfig::kBlobLogs) {
      log_line.Blob(blob);
    }
  }

  [[no_unique_address]] OrEmpty_t<
      SinkConfig::kLevelModuleLogs || SinkConfig::kNameLogs, Tag const&> tag;
  [[no_unique_address]] OrEmpty_t<SinkConfig::kLevelModuleLogs, Level> level;
  [[no_unique_address]] OrEmpty_t<SinkConfig::kLocationLogs, std::string_view>
      file;
  [[no_unique_address]] OrEmpty_t<SinkConfig::kLocationLogs, std::uint32_t>
      line;
  [[no_unique_address]] OrEmpty_t<SinkConfig::kBlobLogs,
                                  std::span<std::uint8_t const>> blob;
};

// Dummy Tele if telemetry is disabled
template <typename TSink, typename TSinkConfig, typename _ = void>
struct Tele {
  template <typename... TArgs>
  constexpr explicit Tele(TArgs&&... /* args */) {}
};

// Tele for enabled telemetry
template <typename TSink, typename TSinkConfig>
struct Tele<TSink, TSinkConfig, std::enable_if_t<kIsAnyTele<TSinkConfig>>> {
  using Sink = TSink;
  using SinkConfig = TSinkConfig;
  using TimedTele = OrEmpty_t<SinkConfig::kTimeMetrics, ae::tele::TimedTele>;

  template <typename... BlobArgs>
  constexpr Tele(Sink& sink, Tag const& tag, [[maybe_unused]] Level level,
                 [[maybe_unused]] std::string_view file,
                 [[maybe_unused]] int line,
                 [[maybe_unused]] FormatScheme const format =
                     collectors_internal::kEmptyFormat,
                 [[maybe_unused]] BlobArgs&&... args) noexcept
      : timed_tele_{std::invoke([&]() -> TimedTele {
          if constexpr (SinkConfig::kTimeMetrics) {
            return TimedTele{.timer = {}, .tag = tag, .trap = sink.trap()};
          } else {
            return TimedTele{};
          }
        })} {
    auto trap = sink.trap();
    if (!trap) {
      return;
    }
    if constexpr (SinkConfig::kCountMetrics) {
      trap->AddInvoke(tag, 1);
    }

    if constexpr (kIsAnyLogs<SinkConfig>) {
      // TODO: more effective way to make blob
      std::string blob_str;
      std::span<std::uint8_t const> blob{};
      if constexpr (SinkConfig::kBlobLogs) {
        if (format.source != collectors_internal::kEmptyFormat.source) {
          FormatTo(blob_str, format, std::forward<BlobArgs>(args)...);
          blob = {reinterpret_cast<std::uint8_t const*>(blob_str.data()),
                  blob_str.size()};
        }
      }
      auto collector = TeleLogCollector<SinkConfig>{
          tag, level, file, static_cast<std::uint32_t>(line), blob};
      trap->LogLine(tag, collector);
    }
  }

  ~Tele() {
    if constexpr (SinkConfig::kTimeMetrics) {
      if (!timed_tele_.trap) {
        return;
      }
      timed_tele_.trap->AddInvokeDuration(timed_tele_.tag,
                                          timed_tele_.timer.elapsed());
    }
  }

 private:
  [[no_unique_address]] TimedTele timed_tele_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_COLLECTORS_H_
