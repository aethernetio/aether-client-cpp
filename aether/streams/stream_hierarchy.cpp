// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/streams/stream_hierarchy.h"
#include <set>
#include <stdexcept>
#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/client.h"
#include "aether/client_messages/p2p_adaptive_datagram_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
namespace ae {
namespace stream_hierarchy_internal {
void Validate(std::vector<StreamDefinition> const& nodes, Uid inherited,
              std::set<std::uint32_t>& ids, unsigned depth = 0) {
  if (depth > 16) {
    throw std::invalid_argument("Stream hierarchy is too deep");
  }
  for (auto const& node : nodes) {
    auto uid = node.destination.empty() ? inherited : node.destination;
    if (uid.empty() || node.id == 0 || !ids.insert(node.id).second ||
        ids.size() > 64 || node.delivery > StreamDelivery::kAdaptiveDatagram) {
      throw std::invalid_argument("Invalid or duplicate stream definition");
    }
    if (node.storage) {
      throw std::logic_error("Offline storage providers are not implemented");
    }
    if (node.routing && !node.routing->lan && !node.routing->direct_ip &&
        !node.routing->cloud) {
      throw std::invalid_argument("Stream has no enabled route");
    }
    Validate(node.children, uid, ids, depth + 1);
  }
}
HierarchicalStream* Find(HierarchicalStream& node, std::uint32_t id) {
  if (node.id() == id) {
    return &node;
  }
  for (auto const& child : node.children()) {
    if (auto* found = Find(*child, id)) {
      return found;
    }
  }
  return nullptr;
}
}  // namespace stream_hierarchy_internal

HierarchicalStream::HierarchicalStream(
    AeContext context, Ptr<Client> const& client, StreamDefinition definition,
    Uid destination, StreamRoutePolicy inherited, PeerService inherited_service)
    : definition_{std::move(definition)} {
  if (!definition_.destination.empty()) {
    destination = definition_.destination;
  }
  if (definition_.service.id.empty()) {
    definition_.service = inherited_service;
  }
  auto policy = definition_.routing.value_or(inherited);
  auto handle = client->message_stream_manager().CreatePort(
      destination, definition_.id, definition_.service);
  if (definition_.delivery == StreamDelivery::kAdaptiveDatagram) {
    delivery_ = std::make_unique<P2pAdaptiveDatagramStream>(
        context, client, destination, std::move(handle),
        adaptive_datagram::Config{}, RequestPolicy::MainServer{}, policy);
    stream_ = delivery_.get();
  } else {
    transport_ = std::make_shared<P2pStream>(
        context, client, destination, std::move(handle),
        RequestPolicy::MainServer{}, policy);
    stream_ = transport_.get();
  }
  if (definition_.delivery == StreamDelivery::kReliable) {
    SafeStreamConfig config{AE_SAFE_STREAM_CAPACITY / 2 - 1,
                            1000,
                            10,
                            std::chrono::seconds{2},
                            std::chrono::milliseconds{5},
                            std::chrono::milliseconds{500}};
    delivery_ = std::make_unique<P2pSafeStream>(context, config, transport_);
    stream_ = delivery_.get();
  }
  for (auto const& child : definition_.children) {
    children_.emplace_back(new HierarchicalStream{
        context, client, child, destination, policy, definition_.service});
  }
}
HierarchicalStream::~HierarchicalStream() = default;
WriteAction& HierarchicalStream::Write(DataBuffer&& data) {
  return stream_->Write(std::move(data));
}
StreamInfo HierarchicalStream::stream_info() const {
  return stream_->stream_info();
}
ByteIStream::StreamUpdateEvent const&
HierarchicalStream::stream_update_event() {
  return stream_->stream_update_event();
}
ByteIStream::OutDataEvent const& HierarchicalStream::out_data_event() {
  return stream_->out_data_event();
}
void HierarchicalStream::Restream() { stream_->Restream(); }
std::array<TransportStatistics, 3> HierarchicalStream::statistics() const {
  if (definition_.delivery == StreamDelivery::kAdaptiveDatagram) {
    return static_cast<P2pAdaptiveDatagramStream*>(delivery_.get())
        ->transport_statistics();
  }
  return transport_->statistics();
}
StreamHierarchy::StreamHierarchy(
    AeContext context, Ptr<Client> const& client,
    std::vector<StreamDefinition> const& definitions) {
  std::set<std::uint32_t> ids;
  stream_hierarchy_internal::Validate(definitions, {}, ids);
  for (auto const& definition : definitions) {
    roots_.emplace_back(new HierarchicalStream{
        context, client, definition, {}, StreamRoutePolicy{}, {}});
  }
}
HierarchicalStream* StreamHierarchy::Find(std::uint32_t id) const {
  for (auto const& root : roots_) {
    if (auto* found = stream_hierarchy_internal::Find(*root, id)) {
      return found;
    }
  }
  return nullptr;
}
}  // namespace ae
