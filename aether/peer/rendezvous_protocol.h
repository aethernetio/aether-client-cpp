// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_RENDEZVOUS_PROTOCOL_H_
#define AETHER_PEER_RENDEZVOUS_PROTOCOL_H_
#include <array>
#include "aether/peer/peer_protocol.h"
#if AE_SUPPORT_P2P
namespace ae {
inline constexpr std::uint32_t kRendezvousStream = 0xfffffff0;
inline PeerService RendezvousServiceId() {
  return {Uid::FromString("a89d9189-9e39-4a22-ae70-112812720101"), 2};
}
struct PunchSocket {
  AE_REFLECT_MEMBERS(id, lane, local)
  Uid id{};
  PunchLane lane{};
  AddressPort local{};
};
struct RendezvousEnrollment {
  AE_REFLECT_MEMBERS(app, instance, profile, socket, standard, dual_socket)
  AppId app{};
  Uid instance{};
  CryptoLib profile{kDefaultCryptoLibProfile};
  PunchSocket socket;
  bool standard{true}, dual_socket{true};
};
struct RendezvousGrant {
  AE_REFLECT_MEMBERS(socket, token, upstream, downstream)
  Uid socket{}, token{};
  std::array<std::uint8_t, 32> upstream{}, downstream{};
  ~RendezvousGrant() {
    ErasePeerSecret(upstream);
    ErasePeerSecret(downstream);
  }
};
struct RendezvousConnect {
  AE_REFLECT_MEMBERS(app, instance, peer, standard, dual_socket)
  AppId app{};
  Uid instance{}, peer{};
  bool standard{true}, dual_socket{true};
};
struct PunchPair {
  AE_REFLECT_MEMBERS(socket, lane, token, local, external, initiator)
  Uid socket{};
  PunchLane lane{};
  Uid token{};
  AddressPort local{}, external{};
  bool initiator{};
};
struct PunchPlan {
  AE_REFLECT_MEMBERS(app, instance, attempt, peer, pairs)
  AppId app{};
  Uid instance{}, attempt{}, peer{};
  std::vector<PunchPair> pairs;
};
enum class RendezvousMethod : MessageId {
  kEnroll = 3,
  kGrant,
  kConnect,
  kOffer,
  kReady,
  kGo
};
enum class RendezvousUdpMethod : std::uint8_t {
  kRegister = 3,
  kChallenge,
  kConfirm,
  kObserved
};
struct RendezvousSignal {
  RendezvousMethod method{};
  DataBuffer body;
};
DataBuffer PackRendezvousSignal(RendezvousMethod method, DataBuffer body);
std::optional<RendezvousSignal> ParseRendezvousSignal(DataBuffer const& wire);
DataBuffer PackRendezvousDatagram(Uid const& token, DataBuffer record);
bool ParseRendezvousDatagram(DataBuffer const& wire, Uid& token,
                             DataBuffer& record);
bool IsRendezvousDatagram(DataBuffer const& wire);
bool ValidRendezvousEndpoint(AddressPort const& endpoint);
}  // namespace ae
#endif
#endif
