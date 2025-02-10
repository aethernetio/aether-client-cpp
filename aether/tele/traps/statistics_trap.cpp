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

#include "aether/tele/traps/statistics_trap.h"

#include <cmath>
#include <cassert>
#include <utility>
#include <iterator>
#include <algorithm>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"

namespace ae::tele {
namespace statistics {

LogStore::size_type LogStore::Size() const { return logs.size(); }
MetricsStore::size_type MetricsStore::Size() const { return metrics.size(); }

template class ProxyStatistics<LogStore>;
template class ProxyStatistics<MetricsStore>;

template <typename T>
ProxyStatistics<T>::ProxyStatistics(RcPtr<Statistics> statistics,
                                    T& data) noexcept
    : statistics_{std::move(statistics)}, data_{&data}, size_{data.Size()} {}

template <typename T>
ProxyStatistics<T>::ProxyStatistics(ProxyStatistics&& other) noexcept
    : statistics_{other.statistics_}, data_{other.data_}, size_(other.size_) {
  // null means moved
  other.data_ = nullptr;
}

template <typename T>
ProxyStatistics<T>::~ProxyStatistics() noexcept {
  if (!data_) {
    return;
  }
  if (size_ >= data_->Size()) {
    return;
  }

  assert(data_->Size() > size_);
  statistics_->UpdateSize(*data_, data_->Size() - size_);
}

template <typename T>
T* ProxyStatistics<T>::operator->() noexcept {
  return data_;
}

std::size_t Statistics::Size() const { return size_; }

LogStore& Statistics::logs() { return logs_; }
MetricsStore& Statistics::metrics() { return metrics_; }

void Statistics::UpdateSize(LogStore const& logs, std::size_t delta_size) {
  auto sum = std::size_t{};
  // get last delta_size elements and calculate size
  auto const& data = logs.logs;
  auto it = data.rbegin();
  for (std::size_t i = 0; i < delta_size; ++i) {
    // this should never fail
    assert(it != data.rend());
    sum += it->size();
  }
  size_ += static_cast<std::uint32_t>(sum);
}

void Statistics::UpdateSize(MetricsStore const& /* metrics */,
                            std::size_t delta_size) {
  // calculate size by delta count
  size_ +=
      static_cast<std::uint32_t>(delta_size * sizeof(MetricsStore::Metric));
}

void Statistics::Append(Statistics const& other) {
  logs_.logs.insert(std::end(logs_.logs), std::begin(other.logs_.logs),
                    std::end(other.logs_.logs));

  for (auto const& [i, m] : other.metrics_.metrics) {
    auto [it, inserted] = metrics_.metrics.try_emplace(i, m);
    if (!inserted) {
      it->second.invocations_count +=
          static_cast<std::uint32_t>(m.invocations_count);
      it->second.max_duration =
          std::max(it->second.max_duration, m.max_duration);
      it->second.sum_duration += m.sum_duration;
      it->second.min_duration =
          std::min(it->second.min_duration, m.min_duration);
    }
  }
}

StatisticsStore::StatisticsStore() : current_{MakeRcPtr<Statistics>()} {};
StatisticsStore::~StatisticsStore() = default;

RcPtr<Statistics> StatisticsStore::Get() {
  if (IsCurrentFull()) {
    Rotate();
  }
  return current_;
}

EnvStore& StatisticsStore::GetEnvStore() { return env_store_; }

void StatisticsStore::Merge(StatisticsStore const& newer) {
  // should always be
  assert(newer.current_);
  // access through Get(), to make rotation if needed
  auto current = Get();
  current->Append(*newer.current_);

  if (newer.prev_) {
    prev_ = newer.prev_;
  }

  env_store_ = newer.env_store_;
}

void StatisticsStore::SetSizeLimit(std::size_t limit) {
  statistics_size_limit_ = static_cast<std::uint32_t>(limit);
}

bool StatisticsStore::IsCurrentFull() const {
  return current_->Size() >= statistics_size_limit_;
}

void StatisticsStore::Rotate() {
  prev_.Reset();
  prev_ = std::move(current_);
  current_ = MakeRcPtr<Statistics>();
}

StatisticsTrap::LogStream::LogStream(ProxyStatistics<LogStore>&& ls,
                                     VectorWriter<PackedSize>&& vw)
    : log_store(std::move(ls)),
      vector_writer(std::move(vw)),
      log_writer{vector_writer} {}

StatisticsTrap::LogStream::LogStream(LogStream&& other) noexcept
    : log_store{std::move(other.log_store)},
      vector_writer{std::move(other.vector_writer)},
      log_writer{vector_writer} {}

StatisticsTrap::LogStream::~LogStream() = default;

void StatisticsTrap::LogStream::index(PackedIndex index) {
  log_writer << index;
}
void StatisticsTrap::LogStream::start_time(TimePoint const& start) {
  // TODO: ambiguous overload for 'operator<<' for TimePoint
  log_writer << start.time_since_epoch().count();
}
void StatisticsTrap::LogStream::level(Level::underlined_t level) {
  log_writer << level;
}
void StatisticsTrap::LogStream::module(Module::underlined_t module) {
  log_writer << module;
}
void StatisticsTrap::LogStream::file(char const* file) { log_writer << file; }
void StatisticsTrap::LogStream::line(PackedLine line) { log_writer << line; }
void StatisticsTrap::LogStream::name(char const* name) { log_writer << name; }

StatisticsTrap::MetricStream::MetricStream(ProxyStatistics<MetricsStore>&& ms,
                                           MetricsStore::Metric& m)
    : metrics_store{std::move(ms)}, metric{m} {}

StatisticsTrap::MetricStream::MetricStream(MetricStream&& other) noexcept =
    default;

StatisticsTrap::MetricStream::~MetricStream() = default;

void StatisticsTrap::MetricStream::add_count(std::uint32_t count) {
  metric.invocations_count += count;
}
void StatisticsTrap::MetricStream::add_duration(std::uint32_t duration) {
  // TODO: check overflow?
  metric.sum_duration += duration;
  metric.max_duration =
      std::max(static_cast<std::uint32_t>(metric.max_duration), duration);
  metric.min_duration =
      std::min(static_cast<std::uint32_t>(metric.min_duration), duration);
}

void StatisticsTrap::EnvStream::platform_type(char const* platform_type) {
  env_store.platform_type = platform_type;
}
void StatisticsTrap::EnvStream::compiler(char const* compiler) {
  env_store.compiler = compiler;
}
void StatisticsTrap::EnvStream::compiler_version(char const* compiler_version) {
  env_store.compiler_version = compiler_version;
}
void StatisticsTrap::EnvStream::compilation_option(CompileOption const& opt) {
  env_store.compile_options.emplace_back(opt.index_, opt.value_);
}
void StatisticsTrap::EnvStream::library_version(char const* library_version) {
  env_store.library_version = library_version;
}
void StatisticsTrap::EnvStream::api_version(char const* api_version) {
  env_store.api_version = api_version;
}

void StatisticsTrap::EnvStream::cpu_type(char const* cpu_type) {
  env_store.cpu_type = cpu_type;
}
void StatisticsTrap::EnvStream::endianness(uint8_t endianness) {
  env_store.endianness = endianness;
}

StatisticsTrap::StatisticsTrap() = default;
StatisticsTrap::~StatisticsTrap() = default;

StatisticsTrap::LogStream StatisticsTrap::log_stream(
    Declaration const& /* decl */) {
  // TODO: use decl
  auto statistics = statistics_store_.Get();
  auto logs = ProxyStatistics{statistics, statistics->logs()};
  auto& entry = logs->logs.emplace_back();
  return {std::move(logs), {entry}};
}

StatisticsTrap::MetricStream StatisticsTrap::metric_stream(
    Declaration const& decl) {
  auto statistics = statistics_store_.Get();
  auto metrics = ProxyStatistics{statistics, statistics->metrics()};
  auto& m =
      metrics->metrics[static_cast<MetricsStore::PackedIndex>(decl.index_)];
  return {std::move(metrics), m};
}

StatisticsTrap::EnvStream StatisticsTrap::env_stream() {
  return {statistics_store_.GetEnvStore()};
}

void StatisticsTrap::MergeStatistics(StatisticsTrap const& newer) {
  statistics_store_.Merge(newer.statistics_store_);
}

}  // namespace statistics
}  // namespace ae::tele
