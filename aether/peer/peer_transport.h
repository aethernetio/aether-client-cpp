// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_TRANSPORT_H_
#define AETHER_PEER_PEER_TRANSPORT_H_
#include <memory>
#include <optional>
#include <vector>
#include "aether/ae_context.h"
#include "aether/client_connectivity_policy.h"
#include "aether/events/events.h"
#include "aether/peer/peer_types.h"
#include "aether/peer/peer_udp.h"
#include "aether/peer/rendezvous_protocol.h"
#include "aether/remote_presence.h"
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
                PeerSettings const& settings, Key root,
                Ptr<ClientConnectivityPolicy> const& connectivity);
  ~PeerTransport();
  PeerTransport(PeerTransport const&) = delete;
  PeerTransport& operator=(PeerTransport const&) = delete;
  void Connect(DirectPeer const& peer);
  void SetKeepBackupChannels(Uid const& peer, bool enabled);
  bool Send(Uid const& uid, DataBuffer const& data, bool lan = true,
            bool direct_ip = true, PeerRoute* selected_route = nullptr,
            bool hole_punching = true);
  std::optional<PeerPath> BestPath(Uid const& uid, bool lan = true,
                                   bool direct_ip = true,
                                   bool hole_punching = true) const;
  std::vector<PunchSocket> punch_sockets() const;
  bool SendRendezvous(Uid const& socket, AddressPort const& endpoint,
                      DataBuffer const& data);
  Event<void(Uid const&, PeerDatagram const&)> const&
  rendezvous_datagram_event() const;
  bool PreparePunch(PunchPlan const& plan);
  bool BeginPunch(PunchPlan const& plan);
  bool NeedsHolePunch(Uid const& peer) const;
  OnlineType OnlineTypes(Uid const* peer = nullptr) const noexcept;
  PeerPresenceState PresenceState(Uid peer) const noexcept;
  RxTimingConf RxTiming(Uid peer) const;
  Duration OfflineDetectionTimeout() const;
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
