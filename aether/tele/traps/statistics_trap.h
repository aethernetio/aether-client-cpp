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

#include <map>
#include <list>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include <variant>

#include "third_party/aethernet-numeric/numeric/tiered_int.h"

#include "aether/config.h"
#include "aether/common.h"
#include "aether/mstream.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/tele/itrap.h"
#include "aether/mstream_buffers.h"
#include "aether/reflect/reflect.h"

namespace ae::tele {
namespace statistics {
/**
 * \brief Storage for old saved logs
 */
struct SavedLog {
  AE_REFLECT_MEMBERS(data)

  std::vector<std::uint8_t> data;
};

/**
 * \brief Stores serialized lines of logs in bunch of buffers
 */
struct RuntimeLog {
  using log_entry = std::vector<std::uint8_t>;
  using list_type = std::list<log_entry>;
  using size_type = list_type::size_type;

  RuntimeLog() = default;
  explicit RuntimeLog(SavedLog&& saved);
  void Append(RuntimeLog&& newer);

  std::size_t size{};
  list_type logs;
};

// Convert RuntimeLog to SavedLog and write
template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& stream, RuntimeLog const& log) {
  auto saved = SavedLog{};
  saved.data.reserve(log.size);
  for (auto const& e : log.logs) {
    saved.data.insert(saved.data.end(), e.begin(), e.end());
  }
  stream << saved;
  return stream;
}

/**
 * \brief Storage for logs
 */
using LogStorage = std::variant<SavedLog, RuntimeLog>;

/**
 * \brief Save and load LogStorage.
 * It always saved as SavedLog.
 */
template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& stream, LogStorage const& log) {
  // all variants saved as SavedLog
  std::visit([&](auto const& v) { stream << v; }, log);
  return stream;
}
template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& stream, LogStorage& log) {
  // all variants saved as SavedLog
  log = SavedLog{};
  stream >> std::get<SavedLog>(log);
  return stream;
}

/**
 * \brief Map of telemetry metrics.
 */
struct MetricsStore {
  using PackedIndex = TieredInt<std::uint64_t, std::uint8_t, 250>;
  using PackedValue = TieredInt<std::uint32_t, std::uint8_t, 250>;
  struct Metric {
    PackedValue invocations_count;
    PackedValue max_duration;
    PackedValue sum_duration;
    PackedValue min_duration;

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

/**
 * \brief Storage for telemetry statistics with rotation
 */
class StatisticsStore {
  static constexpr std::size_t kMaxSize = AE_STATISTICS_MAX_SIZE / 2;

 public:
  StatisticsStore();
  ~StatisticsStore();

  EnvStore& env_store();
  MetricsStore& metrics_store();
  RcPtr<LogStorage> log_store();

  /**
   * \brief Merge storage into this
   * \param[in] newer - another storage with newer data
   */
  void Merge(StatisticsStore const& newer);

  void SetSizeLimit(std::size_t limit);

  AE_REFLECT_MEMBERS(statistics_size_limit_, env_store_, metrics_store_,
                     prev_logs_, logs_)

 private:
  bool IsCurrentFull() const;
  void Rotate();

  std::uint32_t statistics_size_limit_{kMaxSize};
  EnvStore env_store_;
  MetricsStore metrics_store_;
  RcPtr<LogStorage> prev_logs_;
  RcPtr<LogStorage> logs_;
};

/**
 * \brief Access to statistics storage through telemetry trap
 */
class StatisticsTrap final : public ITrap {
 public:
  using PackedSize = TieredInt<std::uint64_t, std::uint8_t, 250>;
  using PackedIndex = TieredInt<std::uint64_t, std::uint8_t, 250>;
  using PackedLine = TieredInt<std::uint64_t, std::uint8_t, 250>;

  struct LogLine {
    LogLine(std::unique_lock<std::mutex> l, RcPtr<LogStorage> log,
            std::vector<std::uint8_t>& d);
    ~LogLine();

    std::unique_lock<std::mutex> lock;
    RcPtr<LogStorage> log_storage;
    VectorWriter<PackedSize> vector_writer;
    omstream<VectorWriter<PackedSize>> log_writer;
  };

  StatisticsTrap();
  ~StatisticsTrap() override;

  void AddInvoke(Tag const& tag, std::uint32_t count) override;
  void AddInvokeDuration(Tag const& tag, Duration duration) override;
  void OpenLogLine(Tag const& tag) override;
  void InvokeTime(TimePoint time) override;
  void WriteLevel(Level level) override;
  void WriteModule(Module const& module) override;
  void Location(std::string_view file, std::uint32_t line) override;
  void TagName(std::string_view name) override;
  void Blob(std::uint8_t const* data, std::size_t size) override;
  void CloseLogLine(Tag const& tag) override;
  void WriteEnvData(EnvData const& env_data) override;

  /**
   * \brief Merge newer statistics storage into this
   */
  void MergeStatistics(StatisticsTrap const& newer);

  AE_REFLECT_MEMBERS(statistics_store)

  StatisticsStore statistics_store;

 private:
  std::mutex sync_lock_;
  std::optional<LogLine> log_line_;
};
}  // namespace statistics
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_STATISTICS_TRAP_H_
