// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/streams/stream_hierarchy.h"
#include <set>
#include <stdexcept>
#include "aether/client.h"
#if AE_SUPPORT_ADAPTIVE_DATAGRAM
#  include "aether/client_messages/p2p_adaptive_datagram_stream.h"
#endif
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
#if !AE_SUPPORT_ADAPTIVE_DATAGRAM
    if (node.delivery == StreamDelivery::kAdaptiveDatagram) {
      throw std::invalid_argument("Adaptive datagram support is disabled");
    }
#endif
    if (node.storage) {
      throw std::logic_error("Offline storage providers are not implemented");
    }
    if (node.routing && !(node.routing->lan && AE_SUPPORT_LAN) &&
        !(node.routing->direct_ip && AE_SUPPORT_DIRECT_IP) &&
        !(node.routing->hole_punching && AE_SUPPORT_HOLE_PUNCH) &&
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
    Uid destination, StreamRoutePolicy inherited, PeerService inherited_service,
    StreamSendPolicy inherited_sending)
    : definition_{std::move(definition)} {
  if (!definition_.destination.empty()) {
    destination = definition_.destination;
  }
  if (definition_.service.id.empty()) {
    definition_.service = inherited_service;
  }
  auto policy = definition_.routing.value_or(inherited);
  auto sending = definition_.sending.value_or(inherited_sending);
  sending_ = sending;
  routing_ = policy;
  destination_ = destination;
  auto handle = client->message_stream_manager().CreatePort(
      destination, definition_.id, definition_.service);
#if AE_SUPPORT_ADAPTIVE_DATAGRAM
  if (definition_.delivery == StreamDelivery::kAdaptiveDatagram) {
    delivery_ = std::make_unique<P2pAdaptiveDatagramStream>(
        context, client, destination, std::move(handle),
        adaptive_datagram::Config{}, RequestPolicy::MainServer{}, policy,
        sending);
    stream_ = delivery_.get();
  }
#endif
  if (!stream_) {
    transport_ = std::make_shared<P2pStream>(
        context, client, destination, std::move(handle),
        RequestPolicy::MainServer{}, policy, sending);
    stream_ = transport_.get();
  }
  if (definition_.delivery == StreamDelivery::kReliable) {
    SafeStreamConfig config{AE_SAFE_STREAM_CAPACITY / 2 - 1,
                            AE_SAFE_STREAM_CAPACITY - 8,
                            10,
                            std::chrono::seconds{2},
                            std::chrono::milliseconds{5},
                            std::chrono::milliseconds{500}};
    delivery_ = std::make_unique<P2pSafeStream>(context, config, transport_);
    stream_ = delivery_.get();
  }
  for (auto const& child : definition_.children) {
    children_.emplace_back(
        new HierarchicalStream{context, client, child, destination, policy,
                               definition_.service, sending});
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
std::array<TransportStatistics, kPeerRouteCount>
HierarchicalStream::statistics() const {
#if AE_SUPPORT_ADAPTIVE_DATAGRAM
  if (definition_.delivery == StreamDelivery::kAdaptiveDatagram) {
    return static_cast<P2pAdaptiveDatagramStream*>(delivery_.get())
        ->transport_statistics();
  }
#endif
  return transport_->statistics();
}
StreamHierarchy::StreamHierarchy(
    AeContext context, Ptr<Client> const& client,
    std::vector<StreamDefinition> const& definitions) {
  std::set<std::uint32_t> ids;
  stream_hierarchy_internal::Validate(definitions, {}, ids);
  for (auto const& definition : definitions) {
    roots_.emplace_back(new HierarchicalStream{context,
                                               client,
                                               definition,
                                               {},
                                               StreamRoutePolicy{},
                                               {},
                                               StreamSendPolicy{}});
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
