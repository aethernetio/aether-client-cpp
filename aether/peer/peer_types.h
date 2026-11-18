// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_TYPES_H_
#define AETHER_PEER_PEER_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "aether/config.h"
#include "aether/crypto/crypto_definitions.h"
#include "aether/crypto/key.h"
#include "aether/crypto/sign.h"
#include "aether/crypto/signed_key.h"
#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/types/uid.h"

namespace ae {
using AppId = Uid;
using ServiceId = Uid;

enum class PeerRoute : std::uint8_t { kCloud, kDirectIp, kLan, kHolePunch };
inline constexpr std::size_t kPeerRouteCount = 4;
enum class PunchLane : std::uint8_t { kStandard, kFirst, kSecond };

struct RendezvousSettings {
  AE_REFLECT_MEMBERS(uid, endpoint, standard, dual_socket)
  Uid uid{};
  AddressPort endpoint{};
  bool standard{true};
  bool dual_socket{true};
};

struct PeerCertificate {
  AE_REFLECT_MEMBERS(uid, public_key, signature, server_key)
  Uid uid{};
  Key public_key;
  Sign signature;
  SignedKey server_key;
};

struct PeerIdentity {
  AE_REFLECT_MEMBERS(private_key, certificate, certified)
  Key private_key;
  PeerCertificate certificate;
  bool certified{};
};

struct PeerService {
  AE_REFLECT_MEMBERS(id, version)
  ServiceId id{};
  std::uint16_t version{1};
  bool operator==(PeerService const&) const = default;
};

struct DirectPeer {
  AE_REFLECT_MEMBERS(uid, endpoint)
  Uid uid{};
  AddressPort endpoint{};
};

struct PeerSettings {
  AE_REFLECT_MEMBERS(app_id, services, direct_peers, discovery_port,
                     listen_port, discovery, rendezvous)
  AppId app_id{};
  std::vector<PeerService> services;
  std::vector<DirectPeer> direct_peers;
  std::uint16_t discovery_port{AE_LAN_DISCOVERY_PORT};
  std::uint16_t listen_port{AE_LAN_LISTEN_PORT};
  bool discovery{AE_SUPPORT_LAN != 0};
  std::optional<RendezvousSettings> rendezvous;
};

struct TransportStatistics {
  std::uint64_t sent_messages{};
  std::uint64_t received_messages{};
  std::uint64_t sent_bytes{};
  std::uint64_t received_bytes{};
  std::uint64_t wire_sent_bytes{};
  std::uint64_t wire_received_bytes{};
  std::uint64_t probes_sent{};
  std::uint64_t probes_received{};
  std::uint64_t probes_lost{};
  std::optional<std::uint32_t> rtt_us;
  std::uint32_t jitter_us{};
  std::uint16_t recent_loss_per_mille{};
  std::uint8_t loss_samples{};

  double loss() const {
    auto const completed = probes_received + probes_lost;
    return completed == 0 ? 0.0
                          : static_cast<double>(probes_lost) /
                                static_cast<double>(completed);
  }
};

struct PeerPath {
  Uid uid{};
  Uid session{};
  AddressPort remote{};
  Address local{};
  PeerRoute route{};
  PunchLane punch_lane{};
  bool authenticated{};
  bool connected{};
  TransportStatistics statistics;
};

struct PeerInfo {
  Uid uid{};
  std::vector<PeerService> services;
  std::vector<PeerPath> paths;
};
struct PeerCandidate {
  Uid token{};
  AddressPort endpoint{};
  std::vector<PeerService> services;
};
}  // namespace ae
#endif
