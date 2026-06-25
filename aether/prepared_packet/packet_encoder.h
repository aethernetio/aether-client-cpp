/*
 * Prepared packet encoder experiment.
 *
 * This code does not send anything.
 * It only builds Aether packet bytes from prepared state + payload.
 */
#ifndef AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
#define AETHER_PREPARED_PACKET_PACKET_ENCODER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include "aether/types/uid.h"
#include "aether/types/data_buffer.h"

#include "aether/crypto/key.h"
#include "aether/crypto/crypto_nonce.h"
#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/sync_crypto_provider.h"

#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/sub_api.h"

#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/work_server_api/login_api.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

namespace ae::prepared_packet {

enum class PreparedIpVersion : std::uint8_t {
  kIpV4 = 4,
  kIpV6 = 6,
};

struct PreparedUdpEndpoint {
  PreparedIpVersion version = PreparedIpVersion::kIpV4;
  std::uint16_t port = 0;

  // IPv4 uses first 4 bytes.
  // IPv6 uses all 16 bytes.
  std::array<std::uint8_t, 16> ip{};
};

enum class EncodePacketError {
  kNone = 0,
  kNonceExhausted,
  kEncodeFailed,
};

inline char const* ToString(EncodePacketError error) {
  switch (error) {
    case EncodePacketError::kNone:
      return "none";
    case EncodePacketError::kNonceExhausted:
      return "nonce_exhausted";
    case EncodePacketError::kEncodeFailed:
      return "encode_failed";
  }
  return "unknown";
}

struct EncodePacketResult {
  EncodePacketError error = EncodePacketError::kNone;
  std::size_t bytes_written = 0;

  // Debug counter inside prepared block. This is not the cryptographic nonce value.
  std::uint64_t nonce_index = 0;

  explicit operator bool() const {
    return error == EncodePacketError::kNone;
  }
};

// One block for now.
// On MCU this can live in RTC RAM.
// Later it can be split into flash/static and rtc/mutable parts.
struct PreparedSendMessageBlock {
  // Used by external sender after EncodePacket().
  // EncodePacket() itself does not use endpoint.
  PreparedUdpEndpoint endpoint;

  // Used for login_by_alias(...)
  Uid sender_ephemeral_uid;

  // Used for AuthorizedApi::send_message(AeMessage{target_uid, payload})
  Uid target_uid;

  // Already derived client -> server key.
  Key client_to_server_key;

  // Mutable nonce state.
  CryptoNonce next_nonce;
  std::uint32_t nonce_left = 0;

  // Debug only.
  std::uint64_t nonce_index = 0;
};

class PreparedSendMessageKeyProvider final : public ISyncKeyProvider {
 public:
  explicit PreparedSendMessageKeyProvider(PreparedSendMessageBlock& block)
      : block_{&block} {}

  Key GetKey() const override {
    return block_->client_to_server_key;
  }

  CryptoNonce const& Nonce() const override {
    return block_->next_nonce;
  }

 private:
  PreparedSendMessageBlock* block_;
};

inline EncodePacketResult EncodePacket(PreparedSendMessageBlock& block,
                                       DataBuffer const& payload,
                                       DataBuffer& out) {
  if (block.nonce_left == 0) {
    out.clear();
    return EncodePacketResult{EncodePacketError::kNonceExhausted, 0,
                              block.nonce_index};
  }

  // Match existing ClientKeyProvider semantics:
  // consume next nonce before encryption.
  block.next_nonce.Next();
  --block.nonce_left;

  auto nonce_index = block.nonce_index;
  ++block.nonce_index;

  auto key_provider =
      std::make_unique<PreparedSendMessageKeyProvider>(block);
  SyncEncryptProvider encrypt_provider{std::move(key_provider)};

  ProtocolContext protocol_context;
  LoginApi login_api{protocol_context, encrypt_provider};

  auto api_context = ApiContext{login_api};

  api_context->login_by_alias(
      block.sender_ephemeral_uid,
      SubApi<AuthorizedApi>{
          [&block, &payload](ApiContext<AuthorizedApi>& auth_api) {
            auth_api->send_message(
                AeMessage{block.target_uid, DataBuffer{payload}});
          }});

  out = std::move(api_context).Pack();

  return EncodePacketResult{EncodePacketError::kNone, out.size(), nonce_index};
}

// Existing internal seam. Keep normal Aether path working.
struct PreparedPacketContext {
  std::uint64_t next_nonce = 0;
  std::uint64_t nonce_limit = (std::numeric_limits<std::uint64_t>::max)();

  EncodePacketResult TakeNonce(std::size_t bytes_written) {
    if (next_nonce == nonce_limit) {
      return EncodePacketResult{EncodePacketError::kNonceExhausted, 0,
                                next_nonce};
    }

    auto nonce = next_nonce;
    ++next_nonce;

    return EncodePacketResult{EncodePacketError::kNone, bytes_written, nonce};
  }
};

template <typename TApi>
EncodePacketResult EncodePacket(PreparedPacketContext& prepared,
                                ApiContext<TApi>&& data,
                                DataBuffer& out) {
  out = std::move(data).Pack();
  return prepared.TakeNonce(out.size());
}

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PACKET_ENCODER_H_
