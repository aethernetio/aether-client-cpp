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

#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace ae::tele {

// print any integral to LogStorage
template <typename T>
  requires(std::is_integral_v<T>)
ILogStorage& operator<<(ILogStorage& out, T v) {
  out.Write(reinterpret_cast<std::uint8_t const*>(&v), sizeof(T));
  return out;
}

template <typename T>
concept IndexSerializable = requires(ILogStorage& log_storage, T const t) {
  { t.Serialize(log_storage) };
};

template <typename T>
concept IndexBuffSerializable = requires(std::uint8_t* buf, T const t) {
  { t.Serialize(buf) } -> std::same_as<std::size_t>;
};

template <typename T>
  requires(IndexSerializable<T> || IndexBuffSerializable<T>)
void WriteIndex(ILogStorage& log_storage, T const& v) {
  if constexpr (IndexSerializable<T>) {
    v.Serialize(log_storage);
  } else {
    std::array<std::uint8_t, sizeof(std::uint64_t)> buff;
    auto s = v.Serialize(buff.data());
    log_storage.Write(buff.data(), s);
  }
}

StatisticsTrapBasic::LogLineWriter::LogLineWriter(Tag const& tag,
                                                  ILogStorage& log_storage)
    : log_storage_{log_storage} {
  auto tag_index = MetricsStore::PackedIndex{tag.index()};
  WriteIndex(log_storage_, tag_index);
}

void StatisticsTrapBasic::LogLineWriter::InvokeTime(TimePoint time) {
  auto epoch_ms = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time.time_since_epoch())
          .count());
  log_storage_ << epoch_ms;
}
void StatisticsTrapBasic::LogLineWriter::WriteLevel(Level level) {
  log_storage_ << level.value_;
}
void StatisticsTrapBasic::LogLineWriter::WriteModule(Module const& module) {
  log_storage_ << module.id;
}
void StatisticsTrapBasic::LogLineWriter::Location(std::string_view file,
                                                  std::uint32_t line) {
  log_storage_.Write(reinterpret_cast<std::uint8_t const*>(file.data()),
                     file.size());
  log_storage_ << line;
}
void StatisticsTrapBasic::LogLineWriter::TagName(std::string_view name) {
  log_storage_.Write(reinterpret_cast<std::uint8_t const*>(name.data()),
                     name.size());
}
void StatisticsTrapBasic::LogLineWriter::Blob(
    std::span<std::uint8_t const> blob) {
  if (blob.empty()) {
    return;
  }
  log_storage_.Write(blob.data(), blob.size());
}

StatisticsTrapBasic::StatisticsTrapBasic() = default;
StatisticsTrapBasic::~StatisticsTrapBasic() = default;

void StatisticsTrapBasic::AddInvoke(Tag const& tag, std::uint32_t count) {
  auto lock = std::scoped_lock(sync_lock_);
  metrics_store_.metrics[tag.index()].invocations_count += count;
}

void StatisticsTrapBasic::AddInvokeDuration(Tag const& tag, Duration duration) {
  auto lock = std::scoped_lock(sync_lock_);
  auto& metr = metrics_store_.metrics[tag.index()];

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

void StatisticsTrapBasic::WriteEnvData(EnvData const& env_data) {
  env_store_.platform = env_data.platform_type;
  env_store_.compiler = env_data.compiler;
  env_store_.compiler_version = env_data.compiler_version;
  env_store_.library_version = env_data.library_version;
  env_store_.cpu_arch = env_data.cpu_arch;
  env_store_.endianness = static_cast<std::uint8_t>(env_data.endianness);
  env_store_.utm_id = env_data.utm_id;
  for (auto const& opt : env_data.compile_options) {
    env_store_.compile_options.emplace_back(EnvStore::PackedIndex{opt.index},
                                            std::string{opt.value});
  }
}

void StatisticsTrapBasic::MergeStatistics(StatisticsTrapBasic const& newer) {
  // just steal env_store_
  env_store_ = newer.env_store_;

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

EnvStore const& StatisticsTrapBasic::env_store() const { return env_store_; }
MetricsStore const& StatisticsTrapBasic::metrics_store() const {
  return metrics_store_;
}

}  // namespace ae::tele
