// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_RENDEZVOUS_CLIENT_H_
#define AETHER_PEER_RENDEZVOUS_CLIENT_H_
#include "aether/peer/peer_transport.h"
#include "aether/stream_api/istream.h"
#if AE_SUPPORT_P2P
namespace ae {
class RendezvousClient {
 public:
  RendezvousClient(AeContext context, PeerTransport& transport,
                   PeerSettings const& settings, ByteIStream& signaling);
  ~RendezvousClient();
  RendezvousClient(RendezvousClient const&) = delete;
  RendezvousClient& operator=(RendezvousClient const&) = delete;
  void RequestPeer(Uid const& peer);
  void ReleasePeer(Uid const& peer);
  std::size_t registered_sockets() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
#endif
