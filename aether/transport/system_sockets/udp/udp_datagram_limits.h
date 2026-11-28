// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_DATAGRAM_LIMITS_H_
#define AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_DATAGRAM_LIMITS_H_

#include <cstdint>

namespace ae {
// The cloud relay uses the same limit after serialization and encryption.
// With UDP and IP headers this is 1428 bytes on IPv4 or 1448 bytes on IPv6,
// within 1460- and 1500-byte link MTUs.
inline constexpr std::uint32_t kCloudUdpDatagramPayloadBytes = 1400;
inline constexpr std::uint32_t kCloudUdpReceiveBufferBytes = 1500;
static_assert(kCloudUdpDatagramPayloadBytes <= kCloudUdpReceiveBufferBytes);
}  // namespace ae

#endif  // AETHER_TRANSPORT_SYSTEM_SOCKETS_UDP_UDP_DATAGRAM_LIMITS_H_
