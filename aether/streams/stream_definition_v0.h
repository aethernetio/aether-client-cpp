// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_STREAMS_STREAM_DEFINITION_V0_H_
#define AETHER_STREAMS_STREAM_DEFINITION_V0_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether/streams/stream_definition.h"

namespace ae::stream_definition_internal {

// Client version 0 stored definitions before adaptive policies were introduced.
// Keep its recursive binary layout unchanged when loading saved identities.
struct Version0 {
  AE_REFLECT_MEMBERS(id, name, destination, service, delivery, routing, sending,
                     storage, children)
  std::uint32_t id{};
  std::string name;
  Uid destination{};
  PeerService service;
  StreamDelivery delivery{StreamDelivery::kMessages};
  std::optional<StreamRoutePolicy> routing;
  std::optional<StreamSendPolicy> sending;
  std::optional<OfflineStorageSettings> storage;
  std::vector<Version0> children;

  StreamDefinition Upgrade() && {
    StreamDefinition definition;
    definition.id = id;
    definition.name = std::move(name);
    definition.destination = destination;
    definition.service = service;
    definition.delivery = delivery;
    definition.routing = routing;
    definition.sending = sending;
    definition.storage = storage;
    for (auto& child : children) {
      definition.children.push_back(std::move(child).Upgrade());
    }
    return definition;
  }
};

}  // namespace ae::stream_definition_internal
#endif  // AETHER_STREAMS_STREAM_DEFINITION_V0_H_
