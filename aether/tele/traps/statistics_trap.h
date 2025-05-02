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

#include "aether/config.h"
#include "aether/common.h"
#include "aether/mstream.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/packed_int.h"
#include "aether/tele/modules.h"
#include "aether/format/format.h"
#include "aether/mstream_buffers.h"
#include "aether/tele/declaration.h"
#include "aether/reflect/reflect.h"

namespace ae::tele {
namespace statistics {
struct LogStore {
  using log_entry = std::vector<std::uint8_t>;
  using list_type = std::list<log_entry>;

  using size_type = list_type::size_type;
  size_type Size() const;

  AE_REFLECT_MEMBERS(logs)

  list_type logs;
};

struct MetricsStore {
  using PackedIndex = Packed<std::uint32_t, std::uint8_t, 250>;
  using PackedValue = Packed<std::uint32_t, std::uint8_t, 250>;
  struct Metric {
    PackedValue invocations_count;
    PackedValue max_duration;
    PackedValue sum_duration;
    PackedValue min_duration;

    AE_REFLECT_MEMBERS(invocations_count, max_duration, sum_duration,
                       min_duration)
  };

  using MetricsMap = std::map<PackedIndex, Metric>;
  using size_type = MetricsMap::size_type;

  size_type Size() const;

  AE_REFLECT_MEMBERS(metrics)

  MetricsMap metrics;
};

/**
 * \brief Stores environment information related to the compilation and runtime
 * of the application.
 */
struct EnvStore {
  using PackedIndex = Packed<std::uint32_t, std::uint8_t, 250>;

  std::string platform_type;
  std::string platform_version;
  std::string compiler;
  std::string compiler_version;
  std::string library_version;
  std::string api_version;
  std::string cpu_type;
  std::uint8_t endianness;
  std::uint32_t utm_id;
  std::vector<std::pair<PackedIndex, std::string>> compile_options;

  AE_REFLECT_MEMBERS(platform_type, platform_version, compiler,
                     compiler_version, library_version, api_version, cpu_type,
                     endianness, utm_id, compile_options)
};
}  // namespace statistics
}  // namespace ae::tele

namespace ae::tele {
namespace statistics {
/**
 * \brief Storage for telemetry data
 */
class Statistics {
  template <typename T>
  friend class ProxyStatistics;

 public:
  // Current statistics size in bytes
  std::size_t Size() const;

  LogStore& logs();
  MetricsStore& metrics();

  void Append(Statistics const& other);

  AE_REFLECT_MEMBERS(size_, logs_, metrics_)

 private:
  void UpdateSize(LogStore const& logs, std::size_t delta_size);
  void UpdateSize(MetricsStore const& metrics, std::size_t delta_size);

  LogStore logs_;
  MetricsStore metrics_;
  std::uint32_t size_{};  //< memory size in bytes
};

/**
 * \brief Access to statistics data through proxy object.
 * After proxy freed by user, additional calculations would be performed
 * @see ~Proxy()
 */
template <typename T>
class ProxyStatistics {
 public:
  ProxyStatistics(RcPtr<Statistics> statistics, T& data) noexcept;
  ProxyStatistics(ProxyStatistics const& other) = delete;
  ProxyStatistics(ProxyStatistics&& other) noexcept;
  ~ProxyStatistics() noexcept;

  T* operator->() noexcept;

 private:
  RcPtr<Statistics> statistics_;
  T* data_{};
  typename T::size_type size_{};
};

/**
 * \brief Storage for telemetry statistics with rotation
 */
class StatisticsStore {
  static constexpr std::size_t kMaxSize =
#ifndef STATISTICS_MAX_SIZE
      10 * 1024;  // 10 KB
#else
      STATISTICS_MAX_SIZE;
#endif

 public:
  StatisticsStore();
  ~StatisticsStore();

  /**
   * \brief Get current statistics.
   * If current statistics is full, move it into previous and return new
   * current.
   */
  RcPtr<Statistics> Get();

  EnvStore& GetEnvStore();

  /**
   * \brief Merge storage into this
   * \param[in] newer - another storage with newer data
   */
  void Merge(StatisticsStore const& newer);

  void SetSizeLimit(std::size_t limit);

  AE_REFLECT_MEMBERS(statistics_size_limit_, env_store_, prev_, current_)

 private:
  bool IsCurrentFull() const;
  void Rotate();

  std::uint32_t statistics_size_limit_{kMaxSize};
  EnvStore env_store_;
  RcPtr<Statistics> prev_;
  RcPtr<Statistics> current_;
};

/**
 * \brief Access to statistics storage through telemetry trap
 */

class StatisticsTrap {
 public:
  struct LogStream {
    using PackedSize = Packed<std::uint64_t, std::uint8_t, 250>;
    using PackedIndex = Packed<std::uint64_t, std::uint8_t, 250>;
    using PackedLine = Packed<std::uint64_t, std::uint8_t, 250>;

    LogStream(ProxyStatistics<LogStore>&& ls, VectorWriter<PackedSize>&& vw);
    LogStream(LogStream&&) noexcept;
    ~LogStream();

    void index(PackedIndex index);
    void start_time(TimePoint const& start);
    void level(Level::underlined_t level);
    void module(Module const& module);
    void file(std::string_view file);
    void line(PackedLine line);
    void name(std::string_view name);

    template <typename... TArgs>
    void blob(std::string_view format, TArgs&&... args) {
      log_writer << Format(format, std::forward<TArgs>(args)...);
    }

    ProxyStatistics<LogStore> log_store;
    VectorWriter<PackedSize> vector_writer;
    omstream<VectorWriter<PackedSize>> log_writer;
  };

  struct MetricStream {
    MetricStream(ProxyStatistics<MetricsStore>&& ms,
                 MetricsStore::PackedIndex index);
    MetricStream(MetricStream&& other) noexcept;
    ~MetricStream();

    void add_count(std::uint32_t count);
    void add_duration(std::uint32_t duration);

    ProxyStatistics<MetricsStore> metrics_store;
    MetricsStore::PackedIndex index;
  };

  struct EnvStream {
    void platform_type(char const* platform_type);
    void compiler(char const* compiler);
    void compiler_version(char const* compiler_version);
    void compilation_option(CompileOption const& opt);
    void library_version(char const* library_version);
    void api_version(char const* api_version);
    void cpu_type(char const* cpu_type);
    void endianness(std::uint8_t endianness);
    void utmid(std::uint32_t utm_id);

    EnvStore& env_store;
  };

  StatisticsTrap();
  ~StatisticsTrap();

  LogStream log_stream(Declaration const& decl);
  MetricStream metric_stream(Declaration const& decl);
  EnvStream env_stream();
  std::mutex& sync();

  /**
   * \brief Merge newer statistics storage into this
   */
  void MergeStatistics(StatisticsTrap const& newer);

  AE_REFLECT_MEMBERS(statistics_store_)

  std::mutex sync_lock_;
  StatisticsStore statistics_store_;
};
}  // namespace statistics
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_STATISTICS_TRAP_H_
