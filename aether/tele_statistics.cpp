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

#include "aether/tele_statistics.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>

#include "aether-objects/telemetry.h"
#include "aether-tele/raw/decode.h"
#include "aether/aether_tele.h"

namespace ae {
namespace tele_statistics_internal {
bool ValidRecords(std::span<std::uint8_t const> bytes) {
  auto const decoded = tele::raw::Decode(bytes);
  return decoded.status == tele::raw::DecodeStatus::kOk &&
         decoded.valid_bytes == bytes.size();
}
std::size_t ByteSize(TeleStatisticsArchive const& archive) {
  std::size_t total{};
  for (auto const& [name, space] : archive.spaces) {
    if (space.blocks.size() > std::numeric_limits<std::size_t>::max() - total) {
      return std::numeric_limits<std::size_t>::max();
    }
    total += space.blocks.size();
  }
  return total;
}
}  // namespace tele_statistics_internal

bool ValidTeleStatisticsArchive(TeleStatisticsArchive const& archive) {
  for (auto const& [name, space] : archive.spaces) {
    if (name.empty() || !tele::raw::ValidUtf8(name) ||
        !tele::raw::ValidUtf8(space.catalog) ||
        !tele_statistics_internal::ValidRecords(space.blocks)) {
      return false;
    }
  }
  return true;
}

TeleStatisticsBackend::TeleStatisticsBackend(bool console, std::size_t capacity,
                                             std::string catalog)
    : console_{console}, capacity_{capacity} {
  catalogs_["aether"] = catalog.empty()
                            ? std::string{AetherTeleIdentity::CatalogId()}
                            : std::move(catalog);
#if defined(AE_OBJECTS_RAW_TELE) && AE_OBJECTS_RAW_TELE
  catalogs_["objects"] = objects_tele::Identity::CatalogId();
#endif
}

bool TeleStatisticsBackend::Append(std::string_view space,
                                   std::span<std::uint8_t const> bytes) {
  if (space.empty() || !tele::raw::ValidUtf8(space)) {
    return false;
  }
  std::shared_ptr<TeleStatisticsBackend> forward;
  {
    auto lock = std::scoped_lock{mutex_};
    forward = forward_;
    if (forward == nullptr) {
      auto const size = tele_statistics_internal::ByteSize(archive_);
      if (size > capacity_ || bytes.size() > capacity_ - size) {
        return false;
      }
      tele::raw::DecodeState candidate{writer_times_[std::string{space}]};
      if (tele::raw::Decode(bytes, candidate).status != tele::raw::DecodeStatus::kOk)
        return false;
      auto [entry, inserted] = archive_.spaces.try_emplace(std::string{space});
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
      try {
#endif
        if (inserted) {
          auto const catalog = catalogs_.find(std::string{space});
          if (catalog != catalogs_.end()) {
            entry->second.catalog = catalog->second;
          }
        }
        entry->second.blocks.insert(entry->second.blocks.end(), bytes.begin(),
                                    bytes.end());
        writer_times_[std::string{space}] = std::move(candidate.time);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
      } catch (...) {
        if (inserted) {
          archive_.spaces.erase(entry);
        }
        return false;
      }
#endif
      return true;
    }
  }
  return forward->Append(space, bytes);
}

bool TeleStatisticsBackend::RestoreMetrics(
    std::string_view space, std::uint32_t writer,
    std::span<tele::raw::MetricTotal> output, std::size_t& count) {
  count = 0;
  auto bytes = Snapshot(space);
  auto decoded = tele::raw::Decode(bytes);
  if (decoded.status != tele::raw::DecodeStatus::kOk) {
    return false;
  }
  std::map<std::uint32_t, tele::raw::Metric> totals;
  for (auto const& record : decoded.records) {
    if (record.stream == writer && record.metric.has_value()) {
      tele::raw::AccumulateMetric(totals[record.index], *record.metric);
    }
  }
  if (totals.size() > output.size()) {
    return false;
  }
  for (auto const& [index, metric] : totals) {
    output[count++] = {index, metric};
  }
  return true;
}

std::vector<std::uint8_t> TeleStatisticsBackend::Snapshot(
    std::string_view space) {
  std::shared_ptr<TeleStatisticsBackend> forward;
  {
    auto lock = std::scoped_lock{mutex_};
    forward = forward_;
    if (forward == nullptr) {
      auto const found = archive_.spaces.find(std::string{space});
      return found == archive_.spaces.end() ? std::vector<std::uint8_t>{}
                                            : found->second.blocks;
    }
  }
  return forward->Snapshot(space);
}

TeleStatisticsArchive TeleStatisticsBackend::Snapshot() const {
  std::shared_ptr<TeleStatisticsBackend> forward;
  {
    auto lock = std::scoped_lock{mutex_};
    forward = forward_;
    if (forward == nullptr) {
      return archive_;
    }
  }
  return forward->Snapshot();
}

bool TeleStatisticsBackend::RestoreArchive(TeleStatisticsArchive archive) {
  if (!ValidTeleStatisticsArchive(archive) ||
      tele_statistics_internal::ByteSize(archive) > capacity_) {
    return false;
  }
  for (auto const& [space, catalog] : catalogs_) {
    auto current = archive.spaces.find(space);
    if (current == archive.spaces.end() || current->second.catalog == catalog) {
      continue;
    }
    auto old = std::move(current->second);
    archive.spaces.erase(current);
    auto name = space + "@" + (old.catalog.empty() ? "unmapped" : old.catalog);
    auto archived = archive.spaces.find(name);
    while (archived != archive.spaces.end() &&
           archived->second.catalog != old.catalog) {
      name += "@history";
      archived = archive.spaces.find(name);
    }
    if (archived == archive.spaces.end()) {
      archive.spaces.emplace(std::move(name), std::move(old));
    } else {
      auto& bytes = archived->second.blocks;
      bytes.insert(bytes.end(), old.blocks.begin(), old.blocks.end());
      archived->second.environment = std::move(old.environment);
    }
  }
  auto lock = std::scoped_lock{mutex_};
  if (forward_ != nullptr) {
    return false;
  }
  archive_.spaces.swap(archive.spaces);
  writer_times_.clear();
  for (auto const& [name, value] : archive_.spaces) {
    tele::raw::DecodeState state;
    tele::raw::Decode(value.blocks, state);
    writer_times_[name] = std::move(state.time);
  }
  return true;
}

bool TeleStatisticsBackend::MergeEarly(
    std::shared_ptr<TeleStatisticsBackend> const& early) {
  if (early == nullptr || early.get() == this) {
    return true;
  }
  auto self = shared_from_this();
  auto lock = std::scoped_lock{mutex_, early->mutex_};
  if (forward_ != nullptr) {
    return false;
  }
  if (early->forward_ != nullptr) {
    return early->forward_.get() == this;
  }
  for (auto const& [space, catalog] : catalogs_) {
    auto const current = early->archive_.spaces.find(space);
    if (current != early->archive_.spaces.end() &&
        current->second.catalog != catalog) {
      return false;
    }
  }
  auto const size = tele_statistics_internal::ByteSize(archive_);
  auto const added = tele_statistics_internal::ByteSize(early->archive_);
  if (size > capacity_ || added > capacity_ - size) {
    return false;
  }
  auto merged = archive_;
  for (auto const& [name, source] : early->archive_.spaces) {
    auto& target = merged.spaces[name];
    target.blocks.insert(target.blocks.end(), source.blocks.begin(),
                         source.blocks.end());
    target.environment = source.environment;
    target.catalog = source.catalog;
  }
  archive_.spaces.swap(merged.spaces);
  writer_times_.clear();
  for (auto const& [name, value] : archive_.spaces) {
    tele::raw::DecodeState state;
    tele::raw::Decode(value.blocks, state);
    writer_times_[name] = std::move(state.time);
  }
  early->archive_.spaces.clear();
  early->forward_ = std::move(self);
  return true;
}

void TeleStatisticsBackend::WriteEnvData(std::string_view space,
                                         tele::EnvData const& data) {
  if (space.empty() || !tele::raw::ValidUtf8(space)) {
    return;
  }
  tele::EnvStore environment{};
  environment.platform = data.platform_type;
  environment.compiler = data.compiler;
  environment.compiler_version = data.compiler_version;
  environment.library_version = data.library_version;
  environment.cpu_arch = data.cpu_arch;
  environment.endianness = static_cast<std::uint8_t>(data.endianness);
  environment.utm_id = data.utm_id;
  for (auto const& option : data.compile_options) {
    environment.compile_options.emplace_back(
        tele::EnvStore::PackedIndex{option.index}, std::string{option.value});
  }
  std::shared_ptr<TeleStatisticsBackend> forward;
  {
    auto lock = std::scoped_lock{mutex_};
    forward = forward_;
    if (forward == nullptr) {
      auto& target = archive_.spaces[std::string{space}];
      target.environment = std::move(environment);
      auto const catalog = catalogs_.find(std::string{space});
      if (catalog != catalogs_.end()) {
        target.catalog = catalog->second;
      }
      return;
    }
  }
  forward->WriteEnvData(space, data);
}

void TeleStatisticsBackend::Console(std::string_view space,
                                    std::span<std::uint8_t const> bytes) {
  std::shared_ptr<TeleStatisticsBackend> forward;
  {
    auto lock = std::scoped_lock{mutex_};
    forward = forward_;
  }
  if (forward != nullptr) {
    forward->Console(space, bytes);
    return;
  }
  if (!console_) {
    return;
  }
  auto const decoded = tele::raw::Decode(bytes);
  if (decoded.status != tele::raw::DecodeStatus::kOk) {
    return;
  }
  static std::mutex output_mutex;
  auto lock = std::scoped_lock{output_mutex};
  for (auto const& record : decoded.records) {
    std::cout << space << ' ';
    tele::raw::PrintRecord(std::cout, record);
    if (!record.payload.empty() && tele::raw::ValidUtf8(record.payload)) {
      std::cout.write(reinterpret_cast<char const*>(record.payload.data()),
                      static_cast<std::streamsize>(record.payload.size()));
      std::cout << '\n';
    }
  }
}

std::shared_ptr<tele::raw::IBackend> MakeAetherTelemetryBackend() {
  return std::make_shared<TeleStatisticsBackend>();
}

#ifdef AE_DISTILLATION
TeleStatistics::TeleStatistics(ObjProp prop) : Obj{prop} {}
#endif

#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
void TeleStatistics::Loaded() {
  auto const restored = backend_->RestoreArchive(std::move(loaded_archive_));
  assert(restored && "Failed to restore validated compact telemetry archive");
  static_cast<void>(restored);
}
auto TeleStatistics::backend() const
    -> std::shared_ptr<TeleStatisticsBackend> const& {
  return backend_;
}
#endif
}  // namespace ae
