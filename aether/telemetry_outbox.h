// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TELEMETRY_OUTBOX_H_
#define AETHER_TELEMETRY_OUTBOX_H_
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/binary_archive.h"
namespace ae {
inline constexpr std::size_t kTelemetryMaxArtifactBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kTelemetryMaxOutboxBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kTelemetryMaxPendingBatches = 64;
inline constexpr std::size_t kTelemetryMaxCaptureNamespaces = 64;
using TelemetryBatchId = std::array<std::uint8_t, 16>;
bool ValidTelemetryProduct(std::string_view product);
bool ValidTelemetryArtifact(std::span<std::uint8_t const> artifact);
struct TelemetryBatch {
  TelemetryBatchId id{};
  std::string product;
  std::vector<std::uint8_t> artifact;
  AE_REFLECT_MEMBERS(id, product, artifact)
};
// Ordinary value serialized by the owning TeleStatistics object. Capture
// cursors advance when an immutable batch is enqueued, never on each retry.
struct TelemetryOutbox {
  std::vector<TelemetryBatch> pending;
  std::map<std::string, std::uint64_t> capture_offsets;
  AE_REFLECT_MEMBERS(pending, capture_offsets)
};
}  // namespace ae

namespace ae::seri {
namespace telemetry_outbox_internal {
template <BinaryBuffer B>
SeriResult ReadText(BinaryArchive<B>& archive, std::string& result,
                    std::size_t maximum) {
  std::size_t size{};
  TRY_RESULT(archive.buffer().Read(SizeReadTag{size}));
  if (size > maximum) {
    return Error{container_too_large};
  }
  result.resize(size);
  return archive.buffer().Read(DataReadTag{result.data(), size});
}
inline bool Valid(TelemetryBatch const& batch) {
  return batch.id != TelemetryBatchId{} &&
         ValidTelemetryProduct(batch.product) &&
         ValidTelemetryArtifact(batch.artifact);
}
inline bool ValidCursorName(std::string_view name) {
  return !name.empty() && name.size() <= 128 &&
         name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") ==
             std::string_view::npos;
}
}  // namespace telemetry_outbox_internal
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TelemetryBatch> {
  static constexpr std::size_t kChunkBytes = 64 * 1024;
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TelemetryBatch const> meta) const {
    if (!telemetry_outbox_internal::Valid(meta.value) ||
        meta.value.artifact.size() > kTelemetryMaxArtifactBytes) {
      return Error{container_too_large};
    }
    TRY_RESULT(archive.Save(Meta{meta.value.id}));
    TRY_RESULT(archive.Save(Meta{meta.value.product}));
    auto const& bytes = meta.value.artifact;
    auto chunks =
        bytes.size() / kChunkBytes + (bytes.size() % kChunkBytes != 0);
    TRY_RESULT(archive.buffer().Write(SizeWriteTag{chunks}));
    for (std::size_t offset = 0; offset < bytes.size();) {
      auto count = std::min(kChunkBytes, bytes.size() - offset);
      TRY_RESULT(archive.buffer().Write(SizeWriteTag{count}));
      TRY_RESULT(
          archive.buffer().Write(DataWriteTag{bytes.data() + offset, count}));
      offset += count;
    }
    return Ok{good};
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TelemetryBatch> meta) const {
    return DeseriBounded(archive, meta, kTelemetryMaxArtifactBytes);
  }
  SeriResult DeseriBounded(BinaryArchive<B>& archive, Meta<TelemetryBatch> meta,
                           std::size_t maximum) const {
    TelemetryBatch next;
    TRY_RESULT(archive.Load(Meta{next.id}));
    TRY_RESULT(telemetry_outbox_internal::ReadText(archive, next.product, 16));
    if (next.id == TelemetryBatchId{} || !ValidTelemetryProduct(next.product)) {
      return Error{read_error};
    }
    std::size_t chunks{};
    TRY_RESULT(archive.buffer().Read(SizeReadTag{chunks}));
    if (!chunks ||
        chunks > maximum / kChunkBytes + (maximum % kChunkBytes != 0)) {
      return Error{container_too_large};
    }
    for (std::size_t i = 0; i < chunks; ++i) {
      std::size_t size{};
      TRY_RESULT(archive.buffer().Read(SizeReadTag{size}));
      if (!size || size > kChunkBytes ||
          (i + 1 != chunks && size != kChunkBytes) ||
          size > maximum - next.artifact.size()) {
        return Error{read_error};
      }
      auto offset = next.artifact.size();
      next.artifact.resize(offset + size);
      TRY_RESULT(archive.buffer().Read(
          DataReadTag{next.artifact.data() + offset, size}));
    }
    if (!ValidTelemetryArtifact(next.artifact)) {
      return Error{read_error};
    }
    meta.value = std::move(next);
    return Ok{good};
  }
};
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TelemetryOutbox> {
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TelemetryOutbox const> meta) const {
    auto const& value = meta.value;
    if (value.pending.size() > kTelemetryMaxPendingBatches ||
        value.capture_offsets.size() > kTelemetryMaxCaptureNamespaces) {
      return Error{container_too_large};
    }
    std::size_t total{};
    for (auto it = value.pending.begin(); it != value.pending.end(); ++it) {
      if (!telemetry_outbox_internal::Valid(*it) ||
          it->artifact.size() > kTelemetryMaxOutboxBytes - total ||
          std::any_of(value.pending.begin(), it,
                      [&](auto const& prior) { return prior.id == it->id; })) {
        return Error{container_too_large};
      }
      total += it->artifact.size();
    }
    for (auto const& cursor : value.capture_offsets) {
      if (!telemetry_outbox_internal::ValidCursorName(cursor.first)) {
        return Error{read_error};
      }
    }
    TRY_RESULT(archive.buffer().Write(SizeWriteTag{value.pending.size()}));
    for (auto const& batch : value.pending) {
      TRY_RESULT(archive.Save(batch));
    }
    TRY_RESULT(
        archive.buffer().Write(SizeWriteTag{value.capture_offsets.size()}));
    for (auto const& [name, offset] : value.capture_offsets) {
      TRY_RESULT(archive.Save(name));
      TRY_RESULT(archive.Save(offset));
    }
    return Ok{good};
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TelemetryOutbox> meta) const {
    std::size_t count{};
    TRY_RESULT(archive.buffer().Read(SizeReadTag{count}));
    if (count > kTelemetryMaxPendingBatches) {
      return Error{container_too_large};
    }
    TelemetryOutbox next;
    std::size_t total{};
    for (std::size_t i = 0; i < count; ++i) {
      TelemetryBatch batch;
      TRY_RESULT((Serializer<BinaryArchive<B>, TelemetryBatch>{}.DeseriBounded(
          archive, Meta{batch}, kTelemetryMaxOutboxBytes - total)));
      if (std::any_of(
              next.pending.begin(), next.pending.end(),
              [&](auto const& prior) { return prior.id == batch.id; })) {
        return Error{read_error};
      }
      total += batch.artifact.size();
      next.pending.push_back(std::move(batch));
    }
    TRY_RESULT(archive.buffer().Read(SizeReadTag{count}));
    if (count > kTelemetryMaxCaptureNamespaces) {
      return Error{container_too_large};
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::string name;
      std::uint64_t offset{};
      TRY_RESULT(telemetry_outbox_internal::ReadText(archive, name, 128));
      TRY_RESULT(archive.Load(offset));
      if (!telemetry_outbox_internal::ValidCursorName(name) ||
          !next.capture_offsets.emplace(std::move(name), offset).second) {
        return Error{read_error};
      }
    }
    meta.value = std::move(next);
    return Ok{good};
  }
};
}  // namespace ae::seri

#endif
