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

#ifndef AETHER_TELE_TRAPS_STATISTICS_TRAP_H_
#define AETHER_TELE_TRAPS_STATISTICS_TRAP_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "numeric/tiered_int.h"

#include "aether/tele/itrap.h"

namespace ae::tele {
namespace statistics {
/**
 * \brief Map of telemetry metrics.
 */
struct MetricsStore {
  using PackedIndex = TieredInt<std::uint64_t, std::uint8_t, 250>;
  using PackedCount = TieredInt<std::uint64_t, std::uint8_t, 250>;
  struct Metric {
    PackedCount invocations_count;
    std::uint32_t max_duration;
    std::uint32_t sum_duration;
    std::uint32_t min_duration;

    AE_REFLECT_MEMBERS(invocations_count, max_duration, sum_duration,
                       min_duration)
  };

  using MetricsMap = std::map<PackedIndex, Metric>;

  AE_REFLECT_MEMBERS(metrics)

  MetricsMap metrics;
};

/**
 * \brief Stores environment information related to the compilation and runtime
 * of the application.
 */
struct EnvStore {
  using PackedIndex = TieredInt<std::uint32_t, std::uint8_t, 250>;

  std::string library_version;
  std::string platform;
  std::string compiler;
  std::string compiler_version;
  std::string api_version;
  std::string cpu_arch;
  std::uint8_t endianness;
  std::uint32_t utm_id;
  std::vector<std::pair<PackedIndex, std::string>> compile_options;

  AE_REFLECT_MEMBERS(library_version, platform, compiler, compiler_version,
                     api_version, cpu_arch, endianness, utm_id, compile_options)
};

class ILogStorage {
 protected:
  ~ILogStorage() = default;

 public:
  virtual void Write(std::uint8_t const* data, std::size_t size) = 0;
};

// LogStorage with circular buffer and fixed capacity
template <std::size_t LogCapacity>
struct LogStorage final : public ILogStorage {
  void Write(std::uint8_t const* data, std::size_t count) override {
    count = std::min(count, LogCapacity - 1);

    if (count > LogCapacity - 1 - size()) {
      auto overwrite = count - (LogCapacity - 1 - size());
      start = (start + overwrite) % LogCapacity;
    }

    while (count > 0) {
      auto n = (LogCapacity - pos) > count ? count : LogCapacity - pos;
      std::memcpy(buffer.data() + pos, data, n);
      pos = (pos + n) % LogCapacity;
      data += n;
      count -= n;
    }
  }

  std::size_t size() const {
    if (pos >= start) {
      return pos - start;
    }
    return LogCapacity - start + pos;
  }

  // read from stream
  template <typename TStream>
  friend TStream& operator>>(TStream& in, LogStorage& v) {
    std::uint32_t size;  // NOLINT(*init-variables)
    in >> size;
    assert(size < LogCapacity && "Saved buffer bigger than capacity");
    v.start = 0;
    v.pos = static_cast<std::size_t>(size);
    in.read(v.buffer.data(), static_cast<std::size_t>(size));

    return in;
  }

  // write to stream
  template <typename TStream>
  friend TStream& operator<<(TStream& out, LogStorage const& v) {
    auto size = v.size();
    out << static_cast<std::uint32_t>(size);
    auto s = v.start;
    while (size > 0) {
      auto to_write = (LogCapacity - s) > size ? size : LogCapacity - s;
      out.write(v.buffer.data() + s, to_write);
      s = (s + to_write) % LogCapacity;
      size = size - to_write;
    }

    return out;
  }

  std::size_t start{};
  std::size_t pos{};
  std::array<std::uint8_t, LogCapacity> buffer;
};

class StatisticsTrapBasic : public ITrap {
 public:
  class LogLineWriter final : public ILogLine {
   public:
    LogLineWriter(Tag const& tag, ILogStorage& log_storage);

    void InvokeTime(TimePoint time) override;
    void WriteLevel(Level level) override;
    void WriteModule(Module const& module) override;
    void Location(std::string_view file, std::uint32_t line) override;
    void TagName(std::string_view name) override;
    void Blob(std::span<std::uint8_t const> blob) override;

   private:
    ILogStorage& log_storage_;
  };

  StatisticsTrapBasic();
  ~StatisticsTrapBasic() override;

  void AddInvoke(Tag const& tag, std::uint32_t count) override;
  void AddInvokeDuration(Tag const& tag, Duration duration) override;
  // LogLine implemented not here

  void WriteEnvData(EnvData const& env_data) override;

  void MergeStatistics(StatisticsTrapBasic const& newer);

  EnvStore const& env_store() const;
  MetricsStore const& metrics_store() const;

  AE_REFLECT_MEMBERS(metrics_store_, env_store_)

 protected:
  std::mutex sync_lock_;
  MetricsStore metrics_store_{};
  EnvStore env_store_{};
};

/**
 * \brief Access to statistics storage through telemetry trap
 */
template <std::size_t LogCapacity>
class StatisticsTrap final : public StatisticsTrapBasic {
 public:
  StatisticsTrap() = default;

  void LogLine(Tag const& tag, ILogCollector& log_collector) override {
    auto lock = std::scoped_lock{sync_lock_};
    auto log_writer = StatisticsTrapBasic ::LogLineWriter{tag, log_storage_};
    log_collector.WriteLine(log_writer);
  }

  /**
   * \brief Merge newer statistics storage into this
   */
  void MergeStatistics(StatisticsTrap const& newer) {
    StatisticsTrapBasic::MergeStatistics(newer);
    // write newer logs on top of current
    auto size = newer.log_storage_.size();
    auto s = newer.log_storage_.start;
    while (size > 0) {
      auto to_write = (LogCapacity - s) > size ? size : LogCapacity - s;
      log_storage_.Write(newer.log_storage_.buffer.data() + s, to_write);
      s = (s + to_write) % LogCapacity;
      size = size - to_write;
    }
  }

  LogStorage<LogCapacity> const& log_storage() const { return log_storage_; }

  AE_REFLECT(AE_REF_BASE(StatisticsTrapBasic), AE_MMBRS(log_storage_))

 private:
  LogStorage<LogCapacity> log_storage_;
};
}  // namespace statistics
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_STATISTICS_TRAP_H_
