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

#ifndef AETHER_TELE_STATISTICS_BACKEND_H_
#define AETHER_TELE_STATISTICS_BACKEND_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-tele/raw/backend.h"
#include "aether-tele/traps/statistics_trap.h"
#include "aether/config.h"
#include "aether/types/packed_size.h"

namespace ae {
struct TeleStatisticsNamespace {
  std::string catalog;
  std::vector<std::uint8_t> blocks;
  tele::EnvStore environment{};
  AE_REFLECT_MEMBERS(catalog, blocks, environment)
};

// Only this value is persisted. Streams, callbacks, ownership and locks remain
// runtime state and never enter the object graph.
struct TeleStatisticsArchive {
  std::map<std::string, TeleStatisticsNamespace> spaces;
};

bool ValidTeleStatisticsArchive(TeleStatisticsArchive const& archive);

class TeleStatisticsBackend final
    : public tele::raw::IBackend,
      public std::enable_shared_from_this<TeleStatisticsBackend> {
 public:
  explicit TeleStatisticsBackend(
      bool console = AE_TELE_LOG_CONSOLE,
      std::size_t capacity = std::numeric_limits<std::size_t>::max(),
      std::string catalog = {});

  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override;
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<tele::raw::MetricTotal> output,
                      std::size_t& count) override;
  void Console(std::string_view space,
               std::span<std::uint8_t const> bytes) override;
  void WriteEnvData(std::string_view space, tele::EnvData const& data) override;
  std::vector<std::uint8_t> Snapshot(std::string_view space) override;

  TeleStatisticsArchive Snapshot() const;
  bool RestoreArchive(TeleStatisticsArchive archive);
  // Startup only, with current writers quiescent/drained. Transfer each early
  // archive once; late callbacks from scopes retaining early are forwarded.
  bool MergeEarly(std::shared_ptr<TeleStatisticsBackend> const& early);

 private:
  mutable std::mutex mutex_;
  TeleStatisticsArchive archive_;
  std::shared_ptr<TeleStatisticsBackend> forward_;
  std::map<std::string, std::string> catalogs_;
  bool const console_;
  std::size_t const capacity_;
};

std::shared_ptr<tele::raw::IBackend> MakeAetherTelemetryBackend();
}  // namespace ae

namespace ae::seri {
// Chunk the byte journal rather than bypassing BinaryArchive's container load
// guard. Every allocation/read is bounded even when a namespace exceeds 1 MiB.
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TeleStatisticsNamespace> {
  static constexpr std::size_t kChunkBytes = 64 * 1024;
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TeleStatisticsNamespace const> meta) const {
    TRY_RESULT(archive.Save(Meta{meta.value.catalog}));
    auto const& bytes = meta.value.blocks;
    auto const chunks =
        bytes.size() / kChunkBytes + (bytes.size() % kChunkBytes != 0);
    TRY_RESULT(archive.buffer().Write(SizeWriteTag{chunks}));
    for (std::size_t offset = 0; offset < bytes.size();) {
      auto const size = std::min(kChunkBytes, bytes.size() - offset);
      TRY_RESULT(archive.buffer().Write(SizeWriteTag{size}));
      TRY_RESULT(
          archive.buffer().Write(DataWriteTag{bytes.data() + offset, size}));
      offset += size;
    }
    return archive.Save(Meta{meta.value.environment});
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TeleStatisticsNamespace> meta) const {
    TeleStatisticsNamespace next;
    TRY_RESULT(archive.Load(Meta{next.catalog}));
    std::size_t chunks{};
    TRY_RESULT(archive.buffer().Read(SizeReadTag{chunks}));
    if (chunks > archive.max_container_load_size()) {
      return Error{container_too_large};
    }
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
      std::size_t size{};
      TRY_RESULT(archive.buffer().Read(SizeReadTag{size}));
      if (size == 0 || size > kChunkBytes ||
          (chunk + 1 != chunks && size != kChunkBytes) ||
          size > std::numeric_limits<std::size_t>::max() - next.blocks.size()) {
        return Error{read_error};
      }
      auto const offset = next.blocks.size();
      next.blocks.resize(offset + size);
      TRY_RESULT(archive.buffer().Read(
          DataReadTag{next.blocks.data() + offset, size}));
    }
    TRY_RESULT(archive.Load(Meta{next.environment}));
    meta.value = std::move(next);
    return Ok{good};
  }
};
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TeleStatisticsArchive> {
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TeleStatisticsArchive const> meta) const {
    return archive.Save(Meta{meta.value.spaces});
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TeleStatisticsArchive> meta) const {
    TeleStatisticsArchive next;
    TRY_RESULT(archive.Load(Meta{next.spaces}));
    if (!ValidTeleStatisticsArchive(next)) {
      return Error{SeriError{.error_code = 1010,
                             .message = "Invalid compact telemetry archive"}};
    }
    meta.value = std::move(next);
    return Ok{good};
  }
};
}  // namespace ae::seri
#endif  // AETHER_TELE_STATISTICS_BACKEND_H_
