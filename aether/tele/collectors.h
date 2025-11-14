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

template <typename TSinkConfig, typename Enabled = void>
struct TeleLogCollector;

template <typename TSinkConfig>
struct TeleLogCollector<TSinkConfig,
                        std::enable_if_t<!IsAnyLogs<TSinkConfig>()>> {
  template <typename... TArgs>
  explicit TeleLogCollector(TArgs&&...) {}

  void InvokeTime() {}
  void LevelModule([[maybe_unused]] Level) {}
  void Location([[maybe_unused]] char const*, int) {}
  void TagName(std::string_view) {}
  template <typename... TArgs>
  void Blob(std::string_view, TArgs&&...) {}
  void Blob() {}
};

template <typename TSinkConfig>
struct TeleLogCollector<TSinkConfig,
                        std::enable_if_t<IsAnyLogs<TSinkConfig>()>> {
  TeleLogCollector(std::shared_ptr<ITrap> const& trap, Tag const& tag)
      : trap_(trap.get()), tag_(&tag) {
    if (trap_ == nullptr) {
      return;
    }
    trap_->OpenLogLine(*tag_);
  }

  ~TeleLogCollector() {
    if (trap_ == nullptr) {
      return;
    }
    trap_->CloseLogLine(*tag_);
  }

  void InvokeTime() {
    if constexpr (TSinkConfig::kStartTimeLogs) {
      if (trap_ == nullptr) {
        return;
      }
      trap_->InvokeTime(Now());
    }
  }

  void LevelModule([[maybe_unused]] Level level) {
    if constexpr (TSinkConfig::kLevelModuleLogs) {
      if (trap_ == nullptr) {
        return;
      }
      trap_->WriteLevel(level);
      trap_->WriteModule(tag_->module);
    }
  }

  void Location([[maybe_unused]] char const* file, int line) {
    if constexpr (TSinkConfig::kLocationLogs) {
      if (trap_ == nullptr) {
        return;
      }
      trap_->Location(file, static_cast<std::uint32_t>(line));
    }
  }

  void TagName(std::string_view name) {
    if constexpr (TSinkConfig::kNameLogs) {
      if (trap_ == nullptr) {
        return;
      }
      trap_->TagName(name);
    }
  }

  template <typename... TArgs>
  void Blob(std::string_view format, TArgs&&... args) {
    if constexpr (TSinkConfig::kBlobLogs) {
      if (trap_ == nullptr) {
        return;
      }
      auto blob_string = Format(format, std::forward<TArgs>(args)...);
      trap_->Blob(reinterpret_cast<std::uint8_t const*>(blob_string.data()),
                  blob_string.size());
    }
  }

  void Blob() {}

  ITrap* trap_;
  Tag const* tag_;
};

template <typename TSink, typename TSinkConfig, typename _ = void>
struct Tele {
  // Dummy tele
  template <typename... TArgs>
  constexpr explicit Tele(TArgs&&... /* args */) {}

  [[nodiscard]] auto LogCollector() const noexcept {
    return TeleLogCollector<TSinkConfig>{};
  }
};

template <typename TSink, typename TSinkConfig>
struct Tele<TSink, TSinkConfig,
            std::enable_if_t<IsAnyTele<TSinkConfig>(), void>> {
  using Sink = TSink;
  using SinkConfig = TSinkConfig;
  using TimedTele = std::conditional_t<SinkConfig::kTimeMetrics,
                                       ae::tele::TimedTele, EmptyNull>;

  constexpr Tele(Sink& sink, Tag const& tag)
      : sink_{&sink}, tag_{&tag}, timed_tele_{std::invoke([&]() -> TimedTele {
          if constexpr (std::is_same_v<ae::tele::TimedTele, TimedTele>) {
            return {{}, tag, sink.trap()};
          } else {
            return {};
          }
        })} {
    if constexpr (SinkConfig::kCountMetrics) {
      auto const& trap = sink.trap();
      if (!trap) {
        return;
      }
      trap->AddInvoke(tag, 1);
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

  [[nodiscard]] auto LogCollector() const noexcept {
    return TeleLogCollector<TSinkConfig>{sink_->trap(), *tag_};
  }

 private:
  Sink* sink_;
  Tag const* tag_;
  TimedTele timed_tele_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_COLLECTORS_H_
