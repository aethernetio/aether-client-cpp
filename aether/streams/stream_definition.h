// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_STREAMS_STREAM_DEFINITION_H_
#define AETHER_STREAMS_STREAM_DEFINITION_H_
#include <optional>
#include <string>
#include <vector>
#include "aether/peer/peer_types.h"
namespace ae {
enum class StreamDelivery : std::uint8_t {
  kMessages,
  kReliable,
  kAdaptiveDatagram
};
struct StreamRoutePolicy {
  AE_REFLECT_MEMBERS(lan, direct_ip, cloud, keep_backup_channels, hole_punching)
  bool lan{true};
  bool direct_ip{true};
  bool cloud{true};
  bool keep_backup_channels{true};
  bool hole_punching{true};
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
  AE_REFLECT_MEMBERS(id, name, destination, service, delivery, routing, storage,
                     children)
  std::uint32_t id{};
  std::string name;
  Uid destination{};
  PeerService service;
  StreamDelivery delivery{StreamDelivery::kMessages};
  std::optional<StreamRoutePolicy> routing;
  std::optional<OfflineStorageSettings> storage;
  std::vector<StreamDefinition> children;
};
}  // namespace ae
#endif
