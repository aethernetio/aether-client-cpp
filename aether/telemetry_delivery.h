// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TELEMETRY_DELIVERY_H_
#define AETHER_TELEMETRY_DELIVERY_H_
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include "aether/ae_context.h"
#include "aether/client.h"
#include "aether/clock.h"
#include "aether/telemetry_outbox.h"
namespace ae {
inline constexpr std::uint32_t kTelemetryDeliveryStream = 0x54454c45;
inline constexpr std::size_t kTelemetryMaxFrameBytes = 4096;
bool ValidTelemetryProduct(std::string_view product);
bool ValidTelemetryArtifact(std::span<std::uint8_t const> artifact);
std::vector<std::uint8_t> EncodeTelemetryArtifact(
    std::string_view manifest_json, std::span<std::uint8_t const> payload);
std::string TelemetrySha256(std::span<std::uint8_t const> bytes);
TelemetryBatchId NewTelemetryBatchId();
std::string TelemetryBatchIdText(TelemetryBatchId const& id);
struct TelemetryDeliveryFrame {
  std::uint8_t version{1},
      kind{};  // 0=data, 1=application ACK after durable store
  TelemetryBatchId id{};
  std::string product;
  std::uint64_t offset{}, total{};
  std::vector<std::uint8_t> bytes;
  AE_REFLECT_MEMBERS(version, kind, id, product, offset, total, bytes)
};
std::vector<std::uint8_t> EncodeTelemetryDeliveryFrame(
    TelemetryDeliveryFrame const& frame);
std::optional<TelemetryDeliveryFrame> DecodeTelemetryDeliveryFrame(
    std::span<std::uint8_t const> bytes);

// Platform-independent delivery state machine. Caller must persist the outbox
// before Request and after ACK removal; a false persist retains the batch.
class TelemetryDeliveryPump {
 public:
  using Persist = std::function<bool()>;
  TelemetryDeliveryPump(TelemetryOutbox& outbox, Persist persist);
  std::optional<std::vector<std::uint8_t>> Request(std::size_t max_frame_bytes);
  void Written(bool success);
  bool Receive(std::span<std::uint8_t const> frame);
  void Retry();

 private:
  TelemetryOutbox* outbox_;
  Persist persist_;
  TelemetryBatchId active_{};
  std::size_t offset_{}, writing_bytes_{};
  bool saved_{}, writing_{};
};

// Real SDK adapter: ordinary client messages routed through Aether cloud relay.
// Keep the owning persistent object and its outbox alive until
// Stop/destruction.
class TelemetryDeliverySession {
 public:
  TelemetryDeliverySession(AeContext const& context, Ptr<Client> const& client,
                           Uid recipient, TelemetryOutbox& outbox,
                           TelemetryDeliveryPump::Persist persist,
                           Duration retry = std::chrono::seconds{5});
  ~TelemetryDeliverySession();
  TelemetryDeliverySession(TelemetryDeliverySession const&) = delete;
  TelemetryDeliverySession& operator=(TelemetryDeliverySession const&) = delete;
  void Pump();
  void Stop();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
