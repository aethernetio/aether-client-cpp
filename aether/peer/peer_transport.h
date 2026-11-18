// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_TRANSPORT_H_
#define AETHER_PEER_PEER_TRANSPORT_H_
#include <memory>
#include <optional>
#include <vector>
#include "aether/ae_context.h"
#include "aether/events/events.h"
#include "aether/peer/peer_types.h"
#if AE_SUPPORT_P2P
namespace ae {
class PeerRegistry {
 public:
  std::vector<PeerInfo> const& clients() const { return clients_; }
  std::vector<PeerCandidate> const& candidates() const { return candidates_; }
  Event<void()> const& changed_event() const { return changed_; }
  explicit PeerRegistry(EventContext auto const& context) : changed_{context} {}

 private:
  friend class PeerTransport;
  std::vector<PeerInfo> clients_;
  std::vector<PeerCandidate> candidates_;
  Event<void()> changed_;
};

class PeerTransport {
 public:
  PeerTransport(AeContext context, PeerIdentity& identity,
                PeerSettings const& settings, Key root);
  ~PeerTransport();
  PeerTransport(PeerTransport const&) = delete;
  PeerTransport& operator=(PeerTransport const&) = delete;
  void Connect(DirectPeer const& peer);
  void SetKeepBackupChannels(Uid const& peer, bool enabled);
  bool Send(Uid const& uid, DataBuffer const& data, bool lan = true,
            bool direct_ip = true, PeerRoute* selected_route = nullptr);
  std::optional<PeerPath> BestPath(Uid const& uid, bool lan = true,
                                   bool direct_ip = true) const;
  PeerRegistry const& registry() const;
  Event<void(Uid const&, DataBuffer const&, PeerRoute)> const& message_event()
      const;
  Event<void()> const& changed_event() const;
  std::vector<AddressPort> listen_endpoints() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
#endif
