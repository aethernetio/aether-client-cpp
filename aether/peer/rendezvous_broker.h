// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_RENDEZVOUS_BROKER_H_
#define AETHER_PEER_RENDEZVOUS_BROKER_H_
#include <functional>
#include <memory>
#include "aether/peer/peer_udp.h"
#include "aether/peer/rendezvous_protocol.h"
#if AE_SUPPORT_P2P
namespace ae {
// Rendezvous state machine. The owner supplies authenticated Æthernet signaling
// and UDP output; registration never trusts an endpoint claimed over signaling.
class RendezvousBroker {
 public:
  using SignalSender = std::function<void(Uid const&, DataBuffer)>;
  using DatagramSender = std::function<void(AddressPort const&, DataBuffer)>;
  RendezvousBroker(SignalSender signal, DatagramSender datagram);
  ~RendezvousBroker();
  RendezvousBroker(RendezvousBroker const&) = delete;
  RendezvousBroker& operator=(RendezvousBroker const&) = delete;
  void ReceiveSignal(Uid const& authenticated_sender, DataBuffer const& wire);
  void ReceiveDatagram(PeerDatagram const& datagram);
  void Update();
  std::size_t registered_sockets() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace ae
#endif
#endif
