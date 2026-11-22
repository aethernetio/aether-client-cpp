// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TELEMETRY_PERSISTENT_STATE_H_
#define AETHER_TELEMETRY_PERSISTENT_STATE_H_
#include <array>
#include "aether/tele_statistics_backend.h"
#include "aether/telemetry_namespaces.h"
#include "aether/telemetry_outbox.h"
namespace ae {
// Boundaries seal the source build of accepted bytes across compatible
// upgrades. New records may share the schema, but retain their own provenance.
struct TelemetrySourceEpoch {
  TelemetryProductInfo source;
  std::map<std::string, std::uint64_t> ends;
  AE_REFLECT_MEMBERS(source, ends)
};
struct TelemetryPersistentState {
  std::string compatibility;
  TeleStatisticsArchive archive;
  TelemetryOutbox outbox;
  bool reset{};  // Runtime outcome; never serialized.
  TelemetryProductInfo source;
  std::vector<TelemetrySourceEpoch> epochs;
};
bool ValidTelemetrySourceInfo(TelemetryProductInfo const& source);
bool ValidTelemetryStateBoundaries(TelemetryPersistentState const& state);
}  // namespace ae
namespace ae::seri {
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TelemetrySourceEpoch> {
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TelemetrySourceEpoch const> meta) const {
    if (!ValidTelemetrySourceInfo(meta.value.source) ||
        meta.value.ends.size() > kTelemetryMaxCaptureNamespaces) {
      return Error{container_too_large};
    }
    TRY_RESULT(archive.Save(meta.value.source));
    TRY_RESULT(archive.buffer().Write(SizeWriteTag{meta.value.ends.size()}));
    for (auto const& [name, end] : meta.value.ends) {
      if (!telemetry_outbox_internal::ValidCursorName(name)) {
        return Error{read_error};
      }
      TRY_RESULT(archive.Save(name));
      TRY_RESULT(archive.Save(end));
    }
    return Ok{good};
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TelemetrySourceEpoch> meta) const {
    TelemetrySourceEpoch next;
    TRY_RESULT(archive.Load(next.source));
    if (!ValidTelemetrySourceInfo(next.source)) {
      return Error{read_error};
    }
    std::size_t count{};
    TRY_RESULT(archive.buffer().Read(SizeReadTag{count}));
    if (count > kTelemetryMaxCaptureNamespaces) {
      return Error{container_too_large};
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::string name;
      std::uint64_t end{};
      TRY_RESULT(telemetry_outbox_internal::ReadText(archive, name, 128));
      TRY_RESULT(archive.Load(end));
      if (!telemetry_outbox_internal::ValidCursorName(name) ||
          !next.ends.emplace(std::move(name), end).second) {
        return Error{read_error};
      }
    }
    meta.value = std::move(next);
    return Ok{good};
  }
};
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, TelemetryPersistentState> {
  static constexpr std::size_t kChunkBytes = 64 * 1024;
  static constexpr std::size_t kMaxBytes = 64 * 1024 * 1024;
  SeriResult Seri(BinaryArchive<B>& archive,
                  Meta<TelemetryPersistentState const> meta) const {
    if (!ValidTelemetryStateBoundaries(meta.value)) {
      return Error{read_error};
    }
    std::vector<std::uint8_t> body;
    auto inner = BinaryArchive{BinaryVectorBuffer<>{body}};
    TRY_RESULT(inner.Save(meta.value.archive));
    TRY_RESULT(inner.Save(meta.value.outbox));
    TRY_RESULT(inner.Save(meta.value.source));
    if (meta.value.epochs.size() > 64) {
      return Error{container_too_large};
    }
    TRY_RESULT(inner.buffer().Write(SizeWriteTag{meta.value.epochs.size()}));
    for (auto const& epoch : meta.value.epochs) {
      TRY_RESULT(inner.Save(epoch));
    }
    if (body.size() > kMaxBytes) {
      return Error{container_too_large};
    }
    // Stable skippable envelope: compatibility is read before inner archives.
    TRY_RESULT(archive.Save(meta.value.compatibility));
    auto const chunks = (body.size() + kChunkBytes - 1) / kChunkBytes;
    TRY_RESULT(archive.buffer().Write(SizeWriteTag{chunks}));
    for (std::size_t offset = 0; offset < body.size();) {
      auto size = std::min(kChunkBytes, body.size() - offset);
      TRY_RESULT(archive.buffer().Write(SizeWriteTag{size}));
      TRY_RESULT(
          archive.buffer().Write(DataWriteTag{body.data() + offset, size}));
      offset += size;
    }
    return Ok{good};
  }
  SeriResult Deseri(BinaryArchive<B>& archive,
                    Meta<TelemetryPersistentState> meta) const {
    TelemetryPersistentState next;
    TRY_RESULT(archive.Load(next.compatibility));
    bool const compatible = next.compatibility == TelemetryCompatibilityId();
    std::size_t chunks{};
    TRY_RESULT(archive.buffer().Read(SizeReadTag{chunks}));
    if (chunks > kMaxBytes / kChunkBytes) {
      return Error{container_too_large};
    }
    std::vector<std::uint8_t> body;
    std::array<std::uint8_t, 1024> discard{};
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
      std::size_t size{};
      TRY_RESULT(archive.buffer().Read(SizeReadTag{size}));
      if (!size || size > kChunkBytes ||
          (chunk + 1 < chunks && size != kChunkBytes)) {
        return Error{read_error};
      }
      if (compatible) {
        auto offset = body.size();
        body.resize(offset + size);
        TRY_RESULT(
            archive.buffer().Read(DataReadTag{body.data() + offset, size}));
      } else {
        while (size) {
          auto part = std::min(size, discard.size());
          TRY_RESULT(archive.buffer().Read(DataReadTag{discard.data(), part}));
          size -= part;
        }
      }
    }
    if (compatible) {
      auto inner = BinaryArchive{BinaryVectorBuffer<>{body}};
      TRY_RESULT(inner.Load(next.archive));
      TRY_RESULT(inner.Load(next.outbox));
      TRY_RESULT(inner.Load(next.source));
      std::size_t epochs{};
      TRY_RESULT(inner.buffer().Read(SizeReadTag{epochs}));
      if (epochs > 64) {
        return Error{container_too_large};
      }
      next.epochs.resize(epochs);
      for (auto& epoch : next.epochs) {
        TRY_RESULT(inner.Load(epoch));
      }
      if (inner.buffer().read_position() != body.size() ||
          !ValidTelemetryStateBoundaries(next)) {
        return Error{read_error};
      }
    } else {
      next.reset = true;
    }
    meta.value = std::move(next);
    return Ok{good};
  }
};
}  // namespace ae::seri
#endif
