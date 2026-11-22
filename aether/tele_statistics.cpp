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
#include "aether/telemetry_delivery.h"
#include "aether/telemetry_namespaces.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

#include "aether-objects/telemetry.h"
#include "aether-tele/raw/decode.h"
#include "aether/aether_tele.h"

namespace ae {
namespace telemetry_capture_internal {
// The capture writer and persisted source validator use the same generated
// manifest reader, so product/build identifiers have identical semantics.
std::optional<std::string_view> Value(std::string_view json,
                                      std::string_view key);
std::optional<std::string> Text(std::string_view json, std::string_view key);
}  // namespace telemetry_capture_internal
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
bool Boundary(std::span<std::uint8_t const> input, std::uint64_t end) {
  if (end > input.size()) {
    return false;
  }
  std::size_t cursor{};
  auto packed = [&]<typename Codec>(std::uint64_t& value) {
    return tele::raw::wire::Read<Codec>(input, cursor, value) ==
           tele::raw::wire::ReadStatus::kOk;
  };
  while (cursor < end) {
    auto kind = input[cursor++];
    std::uint64_t writers{};
    if (kind > tele::raw::wire::kMetricRecord ||
        !packed.template operator()<tele::raw::wire::StreamCodec>(writers) ||
        !writers || writers > input.size() - cursor) {
      return false;
    }
    std::uint32_t fields{};
    if (kind == tele::raw::wire::kLogRecord) {
      if (cursor == input.size()) {
        return false;
      }
      fields = input[cursor++];
      if (fields & ~tele::raw::kAllFields) {
        return false;
      }
    }
    for (std::uint64_t i = 0; i < writers; ++i) {
      std::uint64_t writer{}, size{}, count{};
      if (!packed.template operator()<tele::raw::wire::StreamCodec>(writer) ||
          !packed.template operator()<tele::raw::wire::BlobCodec>(size) ||
          !packed.template operator()<tele::raw::wire::MetricCodec>(count)) {
        return false;
      }
      if (fields & tele::raw::kTime) {
        if (input.size() - cursor < 8) {
          return false;
        }
        cursor += 8;
      }
      if (size > input.size() - cursor) {
        return false;
      }
      cursor += static_cast<std::size_t>(size);
    }
  }
  return cursor == end;
}
}  // namespace tele_statistics_internal

bool ValidTelemetrySourceInfo(TelemetryProductInfo const& source) {
  auto hex = [](std::string_view value) {
    return value.find_first_not_of("0123456789abcdef") ==
           std::string_view::npos;
  };
  if (!ValidTelemetryProduct(source.product) || source.build_id.size() != 64 ||
      !hex(source.build_id) ||
      (source.commit.size() != 40 && source.commit.size() != 64) ||
      !hex(source.commit) || source.manifest_json.empty() ||
      EncodeTelemetryArtifact(source.manifest_json, {}).empty()) {
    return false;
  }
  auto build = telemetry_capture_internal::Value(source.manifest_json, "build");
  return build &&
         telemetry_capture_internal::Text(source.manifest_json, "product") ==
             source.product &&
         telemetry_capture_internal::Text(*build, "build_id") ==
             source.build_id &&
         !source.build_id.empty() && !source.commit.empty();
}
bool ValidTelemetryStateBoundaries(TelemetryPersistentState const& state) {
  if (state.archive.spaces.size() > kTelemetryMaxCaptureNamespaces ||
      tele_statistics_internal::ByteSize(state.archive) > 8 * 1024 * 1024 ||
      state.epochs.size() > 64 ||
      (!state.source.manifest_json.empty() &&
       !ValidTelemetrySourceInfo(state.source))) {
    return false;
  }
  for (auto const& [name, cursor] : state.outbox.capture_offsets) {
    auto found = state.archive.spaces.find(name);
    if (cursor &&
        (found == state.archive.spaces.end() ||
         !tele_statistics_internal::Boundary(found->second.blocks, cursor))) {
      return false;
    }
  }
  std::map<std::string, std::uint64_t> previous;
  for (auto const& epoch : state.epochs) {
    if (!ValidTelemetrySourceInfo(epoch.source) ||
        (!state.source.product.empty() &&
         epoch.source.product != state.source.product)) {
      return false;
    }
    for (auto const& [name, end] : previous) {
      if (end && !epoch.ends.contains(name)) {
        return false;
      }
    }
    for (auto const& [name, end] : epoch.ends) {
      auto found = state.archive.spaces.find(name);
      if (found == state.archive.spaces.end() || end < previous[name] ||
          !tele_statistics_internal::Boundary(found->second.blocks, end)) {
        return false;
      }
      previous[name] = end;
    }
  }
  return true;
}

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
  for (auto const& entry : RegisteredTelemetryNamespaces()) {
    catalogs_[entry.name] = entry.catalog;
  }
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
      // Every writer block includes its own time base. A restart or another
      // writer's append must not depend on a process-local decoder cache.
      if (!tele_statistics_internal::ValidRecords(bytes)) {
        return false;
      }
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
  early->archive_.spaces.clear();
  early->forward_ = std::move(self);
  return true;
}

bool TeleStatisticsBackend::CompactAcknowledged(TelemetryOutbox& outbox) {
  if (!outbox.pending.empty()) {
    return false;
  }
  auto lock = std::scoped_lock{mutex_};
  if (forward_) {
    return false;
  }
  auto next = archive_;
  auto cursors = outbox.capture_offsets;
  for (auto& [name, space] : next.spaces) {
    auto cursor = cursors[name];
    if (!cursor) {
      continue;
    }
    if (cursor > space.blocks.size()) {
      return false;
    }
    auto decoded =
        tele::raw::Decode(std::span<std::uint8_t const>{space.blocks}.first(
            static_cast<std::size_t>(cursor)));
    if (decoded.status != tele::raw::DecodeStatus::kOk) {
      return false;
    }
    std::map<std::uint32_t, std::map<std::uint32_t, tele::raw::Metric>> totals;
    for (auto const& record : decoded.records) {
      if (record.metric) {
        tele::raw::AccumulateMetric(totals[record.stream][record.index],
                                    *record.metric);
      }
    }
    std::vector<std::uint8_t> compacted;
    for (auto const& [writer, tags] : totals) {
      for (auto const& [tag, metric] : tags) {
        std::array<std::uint8_t, 96> frame{};
        std::size_t end = tele::raw::wire::kMaxBlockHeaderBytes;
        if (!tele::raw::wire::Write<tele::raw::wire::TagCodec>(tag, frame,
                                                               end)) {
          return false;
        }
        for (auto value : {metric.count, metric.samples, metric.sum, metric.min,
                           metric.max}) {
          if (!tele::raw::wire::Write<tele::raw::wire::MetricCodec>(
                  value, frame, end)) {
            return false;
          }
        }
        std::array<std::uint8_t, tele::raw::wire::kMaxBlockHeaderBytes>
            header{};
        std::size_t header_size = 0;
        if (!tele::raw::wire::WriteBlockHeader(
                header, header_size, tele::raw::wire::kMetricRecord, 0, writer,
                end - tele::raw::wire::kMaxBlockHeaderBytes, 1)) {
          return false;
        }
        compacted.insert(compacted.end(), header.begin(),
                         header.begin() + header_size);
        compacted.insert(compacted.end(),
                         frame.begin() + tele::raw::wire::kMaxBlockHeaderBytes,
                         frame.begin() + end);
      }
    }
    auto checkpoint = compacted.size();
    auto remaining = std::span{space.blocks}.subspan(
        static_cast<std::size_t>(cursor));
    compacted.insert(compacted.end(), remaining.begin(), remaining.end());
    space.blocks.swap(compacted);
    cursors[name] = checkpoint;
  }
  if (!ValidTeleStatisticsArchive(next) ||
      tele_statistics_internal::ByteSize(next) > capacity_) {
    return false;
  }
  archive_.spaces.swap(next.spaces);
  outbox.capture_offsets.swap(cursors);
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
  epochs_ = std::move(loaded_state_.epochs);
  auto completed = [&](TelemetrySourceEpoch const& epoch) {
    return std::all_of(
        epoch.ends.begin(), epoch.ends.end(), [&](auto const& entry) {
          auto found = loaded_state_.outbox.capture_offsets.find(entry.first);
          return entry.second <=
                 (found == loaded_state_.outbox.capture_offsets.end()
                      ? 0
                      : found->second);
        });
  };
  std::erase_if(epochs_, completed);
  auto const current = GetTelemetryProductInfo();
  if (!loaded_state_.source.manifest_json.empty() &&
      loaded_state_.source.build_id != current.build_id) {
    TelemetrySourceEpoch epoch;
    epoch.source = loaded_state_.source;
    for (auto const& [name, space] : loaded_state_.archive.spaces) {
      epoch.ends[name] = space.blocks.size();
    }
    if (!completed(epoch)) {
      if (epochs_.size() == 64) {
        // Bounded provenance cannot represent a 65th unshipped build. Retain
        // independently self-describing immutable batches, reset mutable
        // telemetry only, and allow ordinary application state to keep saving.
        loaded_state_.archive = {};
        loaded_state_.outbox.capture_offsets.clear();
        loaded_state_.reset = true;
        epochs_.clear();
      } else {
        epochs_.push_back(std::move(epoch));
      }
    }
  }
  // The compatibility envelope has already verified numeric tag semantics.
  // Catalog identities describe provenance, so compatible source revisions
  // can restore metric totals without inventing historical namespace names.
  for (auto const& entry : RegisteredTelemetryNamespaces()) {
    auto found = loaded_state_.archive.spaces.find(entry.name);
    if (found != loaded_state_.archive.spaces.end()) {
      found->second.catalog = entry.catalog;
    }
  }
  auto aether = loaded_state_.archive.spaces.find("aether");
  if (aether != loaded_state_.archive.spaces.end()) {
    aether->second.catalog = AetherTeleIdentity::CatalogId();
  }
  auto const restored =
      backend_->RestoreArchive(std::move(loaded_state_.archive));
  assert(restored && "Failed to restore validated compact telemetry archive");
  static_cast<void>(restored);
  outbox_ = std::move(loaded_state_.outbox);
}
auto TeleStatistics::backend() const
    -> std::shared_ptr<TeleStatisticsBackend> const& {
  return backend_;
}
#endif
}  // namespace ae
