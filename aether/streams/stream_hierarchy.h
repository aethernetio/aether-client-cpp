// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_STREAMS_STREAM_HIERARCHY_H_
#define AETHER_STREAMS_STREAM_HIERARCHY_H_
#include "aether/config.h"
#if AE_ENABLE_STREAM_HIERARCHY
#include <memory>
#include <vector>
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/streams/stream_definition.h"
namespace ae {
class HierarchicalStream final : public ByteIStream {
 public:
  ~HierarchicalStream() override;
  std::uint32_t id() const { return definition_.id; }
  StreamDefinition const& definition() const { return definition_; }
  StreamSendPolicy sending_policy() const { return sending_; }
  StreamRoutePolicy routing_policy() const { return routing_; }
  Uid destination() const { return destination_; }
  std::vector<std::unique_ptr<HierarchicalStream>> const& children() const {
    return children_;
  }
  WriteAction& Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  void Restream() override;
  std::array<TransportStatistics, kPeerRouteCount> statistics() const;

 private:
  friend class StreamHierarchy;
  HierarchicalStream(AeContext context, Ptr<Client> const& client,
                     StreamDefinition definition, Uid destination,
                     StreamRoutePolicy inherited, PeerService inherited_service,
                     StreamSendPolicy inherited_sending);
  StreamDefinition definition_;
  StreamSendPolicy sending_;
  StreamRoutePolicy routing_;
  Uid destination_;
  std::shared_ptr<P2pStream> transport_;
  std::unique_ptr<ByteIStream> delivery_;
  ByteIStream* stream_{};
  std::vector<std::unique_ptr<HierarchicalStream>> children_;
};
class StreamHierarchy {
 public:
  StreamHierarchy(AeContext context, Ptr<Client> const& client,
                  std::vector<StreamDefinition> const& definitions);
  HierarchicalStream* Find(std::uint32_t id) const;
  std::vector<std::unique_ptr<HierarchicalStream>> const& roots() const {
    return roots_;
  }

 private:
  std::vector<std::unique_ptr<HierarchicalStream>> roots_;
};
}  // namespace ae
#endif
#endif
