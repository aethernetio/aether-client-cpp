// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_UDP_H_
#define AETHER_PEER_PEER_UDP_H_
#include <memory>
#include <optional>
#include <vector>
#include "aether/peer/peer_types.h"
#if AE_SUPPORT_P2P
namespace ae {
struct PeerDatagram {
  AddressPort source;
  DataBuffer data;
};
class PeerUdp {
 public:
  static std::vector<Address> Interfaces();
  PeerUdp(Address local, std::uint16_t port, bool discovery = false);
  ~PeerUdp();
  PeerUdp(PeerUdp const&) = delete;
  PeerUdp& operator=(PeerUdp const&) = delete;
  bool Send(AddressPort const& remote, DataBuffer const& data);
  bool Announce(std::uint16_t port, DataBuffer const& data);
  std::optional<PeerDatagram> Receive();
  Address const& local() const { return local_; }
  std::uint16_t port() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Address local_;
};
}  // namespace ae
#endif
#endif
