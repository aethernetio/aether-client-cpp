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

#include <memory>
#include <chrono>
#include <cstdint>
#include <utility>
#include <type_traits>

#include "aether/common.h"
#include "aether/misc/defer.h"
#include "aether/format/format.h"

#include "aether/tele/tags.h"
#include "aether/tele/itrap.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"

namespace ae::tele {
struct Timer {
  Duration elapsed() const {
    auto d = std::chrono::duration<double>{Now() - start};
    return std::chrono::duration_cast<Duration>(d);
  }

  TimePoint const start{Now()};
};

struct EmptyNull {};

struct TimedTele {
  Timer timer;
  Tag const& tag;
  std::shared_ptr<ITrap> trap;
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
  using SinkConfig = TSinkConfig;
  using TimedTele = std::conditional_t<SinkConfig::kTimeMetrics,
                                       ae::tele::TimedTele, EmptyNull>;

  constexpr Tele(Sink& sink, Tag const& tag, Level level,
                 char const* file, int line)
      : Tele(sink, tag) {
    if constexpr (IsAnyLogs<SinkConfig>()) {
      // Start the log line with current tag
      auto const& trap = sink.trap();
      if (!trap) {
        return;
      }
      trap->OpenLogLine(tag);
      defer[&] { trap->CloseLogLine(tag); };

      if constexpr (SinkConfig::kStartTimeLogs) {
        trap->InvokeTime(Now());
      }
      if constexpr (SinkConfig::kLevelModuleLogs) {
        trap->WriteLevel(level);
        trap->WriteModule(tag.module);
      }
      if constexpr (SinkConfig::kLocationLogs) {
        trap->Location(file, static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig::kNameLogs) {
        trap->TagName(tag.name);
      }
    }
  }

  template <typename... TArgs>
  Tele(Sink& sink, Tag const& tag, Level level, char const* file,
       int line, std::string_view format, TArgs&&... args)
      : Tele(sink, tag) {
    if constexpr (IsAnyLogs<SinkConfig>()) {
      auto const& trap = sink.trap();
      if (!trap) {
        return;
      }
      trap->OpenLogLine(tag);
      defer[&] { trap->CloseLogLine(tag); };

      if constexpr (SinkConfig::kStartTimeLogs) {
        trap->InvokeTime(Now());
      }
      if constexpr (SinkConfig::kLevelModuleLogs) {
        trap->WriteLevel(level);
        trap->WriteModule(tag.module);
      }
      if constexpr (SinkConfig::kLocationLogs) {
        trap->Location(file, static_cast<std::uint32_t>(line));
      }
      if constexpr (SinkConfig::kNameLogs) {
        trap->TagName(tag.name);
      }
      if constexpr (SinkConfig::kBlobLogs) {
        auto blob_string = Format(format, std::forward<TArgs>(args)...);
        trap->Blob(reinterpret_cast<std::uint8_t const*>(blob_string.data()),
                   blob_string.size());
      }
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
  constexpr Tele(Sink& sink, Tag const& tag)
      : timed_tele_{[&]() -> TimedTele {
          if constexpr (std::is_same_v<ae::tele::TimedTele, TimedTele>) {
            return {{}, tag, sink.trap()};
          } else {
            return {};
          }
        }()} {
    if constexpr (SinkConfig::kCountMetrics) {
      auto const& trap = sink.trap();
      if (!trap) {
        return;
      }
      trap->AddInvoke(tag, 1);
    }
  }

  TimedTele timed_tele_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_COLLECTORS_H_
