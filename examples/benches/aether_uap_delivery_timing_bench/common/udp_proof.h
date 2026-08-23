/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_H_
#define AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_H_

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/channels/ethernet_channel.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/config.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/types/uid.h"

#if AE_SUPPORT_UDP && defined(WIN_SOCKET_ENABLED)
#  include "aether/transport/system_sockets/sockets/win_udp_socket.h"
#endif

#include "udp_proof_types.h"

namespace ae::bench::uap {

inline std::uint64_t CurrentUdpSocketGeneration() noexcept {
#if AE_SUPPORT_UDP && defined(WIN_SOCKET_ENABLED)
  return WinUdpSocketGeneration();
#else
  return 0;
#endif
}

inline std::string FormatEndpointAddress(Address const& address) {
  std::string out;
  std::visit(
      [&](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IpV4Addr>) {
          out = std::to_string(value.ipv4_value[0]) + "." +
                std::to_string(value.ipv4_value[1]) + "." +
                std::to_string(value.ipv4_value[2]) + "." +
                std::to_string(value.ipv4_value[3]);
        } else if constexpr (std::is_same_v<T, IpV6Addr>) {
          out = "ipv6";
        } else if constexpr (std::is_same_v<T, NamedAddr>) {
#if AE_SUPPORT_CLOUD_DNS
          out = value.name;
#else
          out = "named";
#endif
        } else {
          out = "null";
        }
      },
      address);
  return out;
}

inline std::uint32_t PackIpv4(Address const& address) noexcept {
  std::uint32_t packed = 0;
  std::visit(
      [&](auto const& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, IpV4Addr>) {
          packed = (static_cast<std::uint32_t>(value.ipv4_value[0]) << 24) |
                   (static_cast<std::uint32_t>(value.ipv4_value[1]) << 16) |
                   (static_cast<std::uint32_t>(value.ipv4_value[2]) << 8) |
                   static_cast<std::uint32_t>(value.ipv4_value[3]);
        }
      },
      address);
  return packed;
}

inline ChannelProof MakeChannelProof(ServerId server_id,
                                     ClientServerConnection& connection) {
  ChannelProof proof{};
  proof.present = true;
  proof.server_id = server_id;
  proof.udp_socket_generation = CurrentUdpSocketGeneration();

  auto info = connection.stream_info();
  proof.link_state = info.link_state;
  proof.is_writable = info.is_writable;

  auto channel = connection.server_connection().current_channel();
  if (!channel) {
    proof.protocol = BenchProtocol::kUnknown;
    return proof;
  }

  auto const& props = channel->transport_properties();
  proof.connection_type = props.connection_type;
  proof.reliability = props.reliability;

  auto* ethernet = channel.as<EthernetChannel>();
  if (ethernet == nullptr) {
    proof.protocol = BenchProtocol::kUnknown;
    return proof;
  }

  proof.protocol = ClassifyWireProtocol(ethernet->address.protocol);
  proof.port = ethernet->address.port;
  proof.endpoint = FormatEndpointAddress(ethernet->address.address) + ":" +
                   std::to_string(ethernet->address.port);
  proof.ipv4_packed = PackIpv4(ethernet->address.address);
  return proof;
}

inline std::vector<ChannelProof> CollectCloudConnectionProofs(
    CloudServerConnections& cloud_connection) {
  std::vector<ChannelProof> out;
  for (auto* sc : cloud_connection.servers()) {
    if (sc == nullptr) {
      continue;
    }
    auto* cc = sc->client_connection();
    if (cc == nullptr) {
      continue;
    }
    out.push_back(MakeChannelProof(sc->server_id(), *cc));
  }
  return out;
}

inline ChannelProof FirstPresentProof(
    std::vector<ChannelProof> const& proofs) {
  for (auto const& p : proofs) {
    if (p.present) {
      return p;
    }
  }
  return {};
}

inline ChannelProof CollectOwnCloudProof(Client& client) {
  return FirstPresentProof(
      CollectCloudConnectionProofs(client.cloud_connection()));
}

inline ChannelProof CollectDestinationProofFromCloud(Client& client,
                                                     Cloud::ptr const& cloud) {
  ChannelProof best{};
  if (!cloud) {
    return best;
  }
  auto const& loaded = cloud.Load();
  if (!loaded) {
    return best;
  }
  auto& scm = client.server_connection_manager();
  for (auto const& [sid, entry] : loaded->servers()) {
    (void)entry;
    auto conn = scm.FindInCache(sid);
    if (!conn) {
      continue;
    }
    auto proof = MakeChannelProof(sid, *conn);
    if (!proof.present) {
      continue;
    }
    if (!best.present || (proof.link_state == LinkState::kLinked &&
                          best.link_state != LinkState::kLinked)) {
      best = std::move(proof);
    }
  }
  return best;
}

}  // namespace ae::bench::uap

#endif  // AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_H_
