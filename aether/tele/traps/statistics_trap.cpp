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
#define AETHER_TELE_TELE_H_

#include "aether/tele/traps/statistics_trap.h"

#include <sstream>
#include <cassert>
#include <utility>
#include <iterator>
#include <algorithm>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"

namespace ae::tele::statistics {

RuntimeLog::RuntimeLog(SavedLog&& saved) : size{saved.data.size()} {
  logs.push_back(std::move(std::move(saved).data));
  auto ss = std::ostringstream{};
}

void RuntimeLog::Append(RuntimeLog&& newer) {
  size += newer.size;
  logs.splice(std::end(logs), std::move(std::move(newer).logs));
}

StatisticsStore::StatisticsStore()
    : logs_{MakeRcPtr<LogStorage>(RuntimeLog{})} {};
StatisticsStore::~StatisticsStore() = default;

EnvStore& StatisticsStore::env_store() { return env_store_; }

MetricsStore& StatisticsStore::metrics_store() { return metrics_store_; }

RcPtr<LogStorage> StatisticsStore::log_store() {
  // current log must be a RuntimeLog
  if (logs_->index() != 1) {
    // in case current logs is not a RuntimeLog
    logs_ = MakeRcPtr<LogStorage>(
        RuntimeLog{std::move(std::get<SavedLog>(*logs_))});
  }
  // make rotation
  auto r_logs = std::get<RuntimeLog>(*logs_);
  if (r_logs.size >= statistics_size_limit_) {
    prev_logs_ = std::move(logs_);
    logs_ = MakeRcPtr<LogStorage>(RuntimeLog{});
  }

  return logs_;
}

void StatisticsStore::Merge(StatisticsStore const& newer) {
  // just steal env_store_
  env_store_ = newer.env_store_;

  // merge logs
  // logs_ always should be
  assert(newer.logs_);
  // access through log_store, to make rotation if needed
  auto current = log_store();
  std::get<RuntimeLog>(*current).Append(
      std::move(std::get<RuntimeLog>(*newer.logs_)));

  if (newer.prev_logs_) {
    prev_logs_ = newer.prev_logs_;
  }

  // merge metrics
  for (auto const& [index, metric] : newer.metrics_store_.metrics) {
    auto it = metrics_store_.metrics.find(index);
    if (it == std::end(metrics_store_.metrics)) {
      metrics_store_.metrics[index] = metric;
      continue;
    }
    it->second.invocations_count += metric.invocations_count;
    it->second.max_duration =
        std::max(it->second.max_duration, metric.max_duration);
    it->second.min_duration =
        std::min(it->second.min_duration, metric.min_duration);
    it->second.sum_duration += metric.sum_duration;
  }
}

void StatisticsStore::SetSizeLimit(std::size_t limit) {
  statistics_size_limit_ = static_cast<std::uint32_t>(limit);
}

StatisticsTrap::StatisticsTrap() = default;
StatisticsTrap::~StatisticsTrap() = default;

StatisticsTrap::LogLine::LogLine(std::unique_lock<std::mutex> l,
                                 RcPtr<LogStorage> log,
                                 std::vector<std::uint8_t>& d)
    : lock{std::move(l)},
      log_storage{std::move(log)},
      vector_writer{d},
      log_writer{vector_writer} {}

StatisticsTrap::LogLine::~LogLine() {
  // update log size
  std::get<RuntimeLog>(*log_storage).size += vector_writer.data_.size();
}

void StatisticsTrap::AddInvoke(Tag const& tag, std::uint32_t count) {
  auto lock = std::lock_guard(sync_lock_);
  statistics_store.metrics_store().metrics[tag.index()].invocations_count +=
      count;
}

void StatisticsTrap::AddInvokeDuration(Tag const& tag, Duration duration) {
  auto lock = std::lock_guard(sync_lock_);
  auto& metr = statistics_store.metrics_store().metrics[tag.index()];

  metr.sum_duration += static_cast<std::uint32_t>(duration.count());
  metr.max_duration = std::max(static_cast<std::uint32_t>(metr.max_duration),
                               static_cast<std::uint32_t>(duration.count()));

  if (metr.min_duration == 0) {
    metr.min_duration = static_cast<std::uint32_t>(duration.count());
  } else {
    metr.min_duration = std::min(static_cast<std::uint32_t>(metr.min_duration),
                                 static_cast<std::uint32_t>(duration.count()));
  }
}

void StatisticsTrap::OpenLogLine(Tag const& tag) {
  auto lock = std::unique_lock{sync_lock_};
  auto log_store = statistics_store.log_store();
  auto& runtime_log = std::get<RuntimeLog>(*log_store);
  auto& data = runtime_log.logs.emplace_back();

  log_line_.emplace(std::move(lock), log_store, data);
  // always write an index
  log_line_->log_writer << PackedIndex{tag.index()};
}

void StatisticsTrap::InvokeTime(TimePoint time) {
  assert(log_line_);
  log_line_->log_writer << time;
}

void StatisticsTrap::WriteLevel(Level level) {
  assert(log_line_);
  log_line_->log_writer << level.value_;
}

void StatisticsTrap::WriteModule(Module const& module) {
  assert(log_line_);
  log_line_->log_writer << module.id;
}

void StatisticsTrap::Location(std::string_view file, std::uint32_t line) {
  assert(log_line_);
  log_line_->log_writer << file << line;
}

void StatisticsTrap::TagName(std::string_view name) {
  assert(log_line_);
  log_line_->log_writer << name;
}

void StatisticsTrap::Blob(std::uint8_t const* data, std::size_t size) {
  assert(log_line_);
  log_line_->log_writer.write(static_cast<void const*>(data), size);
}

void StatisticsTrap::CloseLogLine(Tag const& /*tag*/) {
  assert(log_line_);
  auto lock = std::move(log_line_->lock);
  log_line_.reset();
}

void StatisticsTrap::WriteEnvData(EnvData const& env_data) {
  auto& env_store = statistics_store.env_store();
  env_store.platform = env_data.platform_type;
  env_store.compiler = env_data.compiler;
  env_store.compiler_version = env_data.compiler_version;
  env_store.library_version = env_data.library_version;
  env_store.api_version = env_data.api_version;
  env_store.cpu_arch = env_data.cpu_arch;
  env_store.endianness = env_data.endianness;
  env_store.utm_id = env_data.utm_id;
  for (auto const& opt : env_data.compile_options) {
    env_store.compile_options.emplace_back(PackedIndex{opt.index},
                                           std::string{opt.value});
  }
}

void StatisticsTrap::MergeStatistics(StatisticsTrap const& newer) {
  statistics_store.Merge(newer.statistics_store);
}

}  // namespace ae::tele::statistics
