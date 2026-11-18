// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_RENDEZVOUS_SERVICE_H_
#define AETHER_PEER_RENDEZVOUS_SERVICE_H_
#include <memory>
#include "aether-objects/ptr/ptr.h"
#include "aether/ae_context.h"
#include "aether/peer/rendezvous_broker.h"
#if AE_SUPPORT_P2P
namespace ae {
class Client;
// Public-host service attached to an ordinary authenticated Æthernet client.
class RendezvousService {
 public:
  RendezvousService(AeContext context, Ptr<Client> const& client,
                    Address bind_address, std::uint16_t port);
  ~RendezvousService();
  RendezvousService(RendezvousService const&) = delete;
  RendezvousService& operator=(RendezvousService const&) = delete;
  std::uint16_t port() const;
  std::size_t registered_sockets() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif
#endif
