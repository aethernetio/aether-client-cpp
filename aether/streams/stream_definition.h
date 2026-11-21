// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_STREAMS_STREAM_DEFINITION_H_
#define AETHER_STREAMS_STREAM_DEFINITION_H_
#include <optional>
#include <string>
#include <vector>
#include "aether/client_messages/priority_send_queue.h"
#include "aether/peer/peer_types.h"
#include "aether/streams/stream_adaptive_policy.h"
namespace ae {
enum class StreamDelivery : std::uint8_t {
  kMessages,
  kReliable,
  kAdaptiveDatagram
};
struct StreamRoutePolicy {
  AE_REFLECT_MEMBERS(lan, direct_ip, cloud, keep_backup_channels, hole_punching)
  bool lan{AE_SUPPORT_LAN != 0};
  bool direct_ip{AE_SUPPORT_DIRECT_IP != 0};
  bool cloud{true};
  bool keep_backup_channels{true};
  bool hole_punching{AE_SUPPORT_HOLE_PUNCH != 0};
};
// Persistent intent reserved for a future storage provider. Creating a live
// stream with storage configured currently fails explicitly.
struct OfflineStorageSettings {
  AE_REFLECT_MEMBERS(provider_uid, service, fetch_pending_on_client_load)
  Uid provider_uid{};
  PeerService service;
  bool fetch_pending_on_client_load{true};
};
struct StreamDefinition {
  AE_REFLECT_MEMBERS(id, name, destination, service, delivery, routing, sending,
                     storage, children, adaptive)
  std::uint32_t id{};
  std::string name;
  Uid destination{};
  PeerService service;
  StreamDelivery delivery{StreamDelivery::kMessages};
  std::optional<StreamRoutePolicy> routing;
  std::optional<StreamSendPolicy> sending;
  std::optional<OfflineStorageSettings> storage;
  std::vector<StreamDefinition> children;
  // Omitted policies inherit from the parent; an explicit policy replaces the
  // entire inherited policy. Applies to adaptive descendants of any node.
  std::optional<StreamAdaptivePolicy> adaptive;
};
}  // namespace ae
#endif
