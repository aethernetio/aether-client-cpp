// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TELEMETRY_KEEPER_H_
#define AETHER_TELEMETRY_KEEPER_H_
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include "aether/telemetry_delivery.h"
namespace ae {
enum class TelemetryStoreResult { kStored, kDuplicate, kRejected };
// Desktop durable receiver storage. The artifact is committed atomically and
// synced before success. Product/source/batch IDs are separate path components.
class TelemetryKeeperStore {
 public:
  explicit TelemetryKeeperStore(std::filesystem::path root);
  TelemetryStoreResult Store(Uid source, TelemetryBatch const& batch);
  std::filesystem::path PathFor(Uid source, TelemetryBatch const& batch) const;

 private:
  std::filesystem::path root_;
  std::mutex mutex_;
};
class TelemetryKeeperReceiver {
 public:
  explicit TelemetryKeeperReceiver(TelemetryKeeperStore& store);
  ~TelemetryKeeperReceiver();
  // Returns an application ACK only after the full artifact is durable.
  std::optional<DataBuffer> Receive(Uid source,
                                    std::span<std::uint8_t const> frame);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// A dedicated, ordinary Aether client listens to the reserved telemetry port.
class TelemetryKeeperSession {
 public:
  TelemetryKeeperSession(AeContext const& context, Ptr<Client> const& client,
                         TelemetryKeeperStore& store);
  ~TelemetryKeeperSession();
  TelemetryKeeperSession(TelemetryKeeperSession const&) = delete;
  TelemetryKeeperSession& operator=(TelemetryKeeperSession const&) = delete;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
