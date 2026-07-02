/*
 * Prepared send_message block.
 *
 * This is transport-neutral state for encoding a send_message packet.
 * It may contain an endpoint selected by the full Aether client, but it does
 * not own sockets, DNS, connections, channels, or timers.
 */
#ifndef AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_
#define AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/types/uid.h"

#include "aether/crypto/key.h"
#include "aether/crypto/crypto_nonce.h"

namespace ae::prepared_packet {
static constexpr std::uint32_t kMagic = 0x50534456;  // "PSDV"
static constexpr std::uint32_t kVersion = 1;
// Serialized PreparedSendMessageBlock is a few hundred bytes. Keep the RTC
// footprint small enough for ESP32 RTC slow memory (8 KiB on ESP32-C6).
static constexpr std::size_t kMaxPreparedBlockBytes = 512;

struct RetainedPreparedBlock {
  std::uint32_t magic;
  std::uint32_t version;
  std::uint32_t size;
  std::uint32_t checksum;
  std::array<std::uint8_t, kMaxPreparedBlockBytes> bytes;
};

enum class PreparedIpVersion : std::uint8_t {
  kIpV4 = 4,
  kIpV6 = 6,
};

struct PreparedEndpoint {
  AE_REFLECT_MEMBERS(version, protocol, port, ip)
  PreparedIpVersion version = PreparedIpVersion::kIpV4;
  Protocol protocol = Protocol::kUdp;
  std::uint16_t port = 0;

  // IPv4 uses first 4 bytes.
  // IPv6 uses all 16 bytes.
  std::array<std::uint8_t, 16> ip{};
};

struct PreparedSendMessageBlock {
  AE_REFLECT_MEMBERS(endpoint, sender_ephemeral_uid, target_uid,
                     client_to_server_key, next_nonce, nonce_left)
  PreparedEndpoint endpoint;

  Uid sender_ephemeral_uid;
  Uid target_uid;

  Key client_to_server_key;

  CryptoNonce next_nonce;
  std::uint32_t nonce_left = 0;
};

enum class EncodePacketError {
  kNone = 0,
  kNonceExhausted,
};

inline char const* ToString(EncodePacketError error) {
  switch (error) {
    case EncodePacketError::kNone:
      return "none";
    case EncodePacketError::kNonceExhausted:
      return "nonce_exhausted";
  }
  return "unknown";
}

struct EncodePacketResult {
  EncodePacketError error = EncodePacketError::kNone;
  std::size_t bytes_written = 0;

  explicit operator bool() const { return error == EncodePacketError::kNone; }
};

std::optional<PreparedEndpoint> MakePreparedEndpoint(Endpoint const& endpoint);

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PREPARED_SEND_MESSAGE_H_
