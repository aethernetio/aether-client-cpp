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

#ifndef AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_TYPES_H_
#define AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_TYPES_H_

#include <cstdint>
#include <string>

#include "aether/channels/channels_types.h"
#include "aether/config.h"
#include "aether/stream_api/istream.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"

#include "bench_ipc.h"
#include "bench_types.h"

namespace ae::bench::uap {

struct ChannelProof {
  bool present{false};
  ServerId server_id{0};
  BenchProtocol protocol{BenchProtocol::kUnknown};
  std::uint16_t port{0};
  std::uint32_t ipv4_packed{0};
  std::string endpoint;
  ConnectionType connection_type{};
  Reliability reliability{};
  LinkState link_state{};
  bool is_writable{false};
  std::uint64_t udp_socket_generation{0};
};

inline char const* BenchProtocolName(BenchProtocol p) noexcept {
  switch (p) {
    case BenchProtocol::kUdp:
      return "udp";
    case BenchProtocol::kTcp:
      return "tcp";
    case BenchProtocol::kUnknown:
    default:
      return "unknown";
  }
}

inline BenchProtocol ClassifyWireProtocol(Protocol protocol) noexcept {
  if (protocol == Protocol::kUdp) {
    return BenchProtocol::kUdp;
  }
  if (protocol == Protocol::kTcp) {
    return BenchProtocol::kTcp;
  }
  return BenchProtocol::kUnknown;
}

inline bool RefuseTcpSample(BenchProtocol own,
                            BenchProtocol destination) noexcept {
#if defined(AE_UAP_DELIVERY_REQUIRE_UDP) && AE_UAP_DELIVERY_REQUIRE_UDP
  return own == BenchProtocol::kTcp || destination == BenchProtocol::kTcp;
#else
  static_cast<void>(own);
  static_cast<void>(destination);
  return false;
#endif
}

inline bool IsMeasuredProtocolOk(BenchProtocol protocol) noexcept {
#if defined(AE_UAP_DELIVERY_REQUIRE_UDP) && AE_UAP_DELIVERY_REQUIRE_UDP
  return protocol == BenchProtocol::kUdp;
#else
  return protocol == BenchProtocol::kTcp;
#endif
}

inline std::string UnpackIpv4Endpoint(std::uint32_t ipv4,
                                      std::uint16_t port) {
  if (ipv4 == 0) {
    return std::string("?:") + std::to_string(port);
  }
  return std::to_string((ipv4 >> 24) & 0xff) + "." +
         std::to_string((ipv4 >> 16) & 0xff) + "." +
         std::to_string((ipv4 >> 8) & 0xff) + "." +
         std::to_string(ipv4 & 0xff) + ":" + std::to_string(port);
}

// IpcFrame packing for IpcType::kUdpProof (frame stays < 128 bytes):
//   event_kind = UdpProofPath
//   sequence   = server_id
//   offset_ms  = port
//   a          = protocol | type<<8 | reliability<<16 | link<<24 | writable<<32
//   b          = ipv4 packed
//   c          = udp_socket_generation
inline void PackUdpProofFrame(IpcFrame& frame, UdpProofPath path,
                              ChannelProof const& proof) noexcept {
  frame.type = static_cast<std::uint8_t>(IpcType::kUdpProof);
  frame.event_kind = static_cast<std::uint8_t>(path);
  frame.sequence = proof.server_id;
  frame.offset_ms = proof.port;
  frame.a = static_cast<std::int64_t>(proof.protocol) |
            (static_cast<std::int64_t>(proof.connection_type) << 8) |
            (static_cast<std::int64_t>(proof.reliability) << 16) |
            (static_cast<std::int64_t>(proof.link_state) << 24) |
            (static_cast<std::int64_t>(proof.is_writable ? 1 : 0) << 32);
  frame.b = static_cast<std::int64_t>(proof.ipv4_packed);
  frame.c = static_cast<std::int64_t>(proof.udp_socket_generation);
}

inline ChannelProof UnpackUdpProofFrame(IpcFrame const& frame) noexcept {
  ChannelProof proof{};
  proof.present = true;
  proof.server_id = static_cast<ServerId>(frame.sequence);
  proof.port = static_cast<std::uint16_t>(frame.offset_ms);
  proof.protocol = static_cast<BenchProtocol>(frame.a & 0xff);
  proof.connection_type =
      static_cast<ConnectionType>((frame.a >> 8) & 0xff);
  proof.reliability = static_cast<Reliability>((frame.a >> 16) & 0xff);
  proof.link_state = static_cast<LinkState>((frame.a >> 24) & 0xff);
  proof.is_writable = ((frame.a >> 32) & 0x1) != 0;
  proof.ipv4_packed = static_cast<std::uint32_t>(frame.b);
  proof.udp_socket_generation = static_cast<std::uint64_t>(frame.c);
  proof.endpoint = UnpackIpv4Endpoint(proof.ipv4_packed, proof.port);
  return proof;
}

}  // namespace ae::bench::uap

#endif  // AETHER_UAP_DELIVERY_TIMING_BENCH_COMMON_UDP_PROOF_TYPES_H_
