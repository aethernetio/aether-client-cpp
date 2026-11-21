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
enum class PeerSendResult { kSent, kBlocked, kError, kTooLarge };
class PeerUdp {
 public:
  static std::vector<Address> Interfaces();
  PeerUdp(Address local, std::uint16_t port, bool discovery = false,
          bool no_fragment = false);
  ~PeerUdp();
  PeerUdp(PeerUdp const&) = delete;
  PeerUdp& operator=(PeerUdp const&) = delete;
  bool Send(AddressPort const& remote, DataBuffer const& data);
  PeerSendResult TrySend(AddressPort const& remote, DataBuffer const& data);
#  if AE_SUPPORT_LAN
  bool Announce(std::uint16_t port, DataBuffer const& data);
#  endif
  std::optional<PeerDatagram> Receive();
  Address const& local() const { return local_; }
  std::uint16_t port() const;

 private:
  friend struct PeerUdpTestHelper;
  static PeerSendResult ClassifySendFailure(int error);
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Address local_;
};
}  // namespace ae
#endif
#endif
