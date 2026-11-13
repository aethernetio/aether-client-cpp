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

#include "aether/client_messages/p2p_stream_write_budget.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "aether/config.h"
#include "aether/types/packed_size.h"
#include "aether/types/uid.h"

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
#  include <sodium/crypto_aead_chacha20poly1305.h>
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
#  include <hydrogen.h>
#endif

namespace ae {
namespace p2p_stream_write_budget_internal {

constexpr std::optional<std::size_t> EncryptOverhead() {
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  return crypto_aead_chacha20poly1305_ABYTES +
         crypto_aead_chacha20poly1305_NPUBBYTES;
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  return hydro_secretbox_HEADERBYTES + sizeof(std::uint64_t);
#else
  return std::nullopt;
#endif
}

bool AddSize(std::size_t& total, std::size_t bytes) {
  if (bytes > std::numeric_limits<std::size_t>::max() - total) {
    return false;
  }
  total += bytes;
  return true;
}

std::optional<std::size_t> PackedLengthBytes(std::size_t size) {
  if (static_cast<std::uint64_t>(size) > PackedSize::kUpper) {
    return std::nullopt;
  }
  std::uint8_t bytes[PackedSize::kMaxWireBytes];
  return PackedSize{size}.Serialize(bytes);
}

std::optional<std::size_t> EncodedBytes(std::size_t payload_size) {
  // EncodePacket packs SendMessage(destination UID, sized payload), encrypts
  // it with a fixed-size tag/nonce, then packs LoginByAlias(sender UID, sized
  // ciphertext). Count those fields without allocating or encrypting payloads.
  // Both method IDs are uint8_t and UIDs have a fixed 16-byte wire encoding.
  auto const overhead = EncryptOverhead();
  auto const payload_length = PackedLengthBytes(payload_size);
  if (!overhead || !payload_length) {
    return std::nullopt;
  }
  auto ciphertext_size = payload_size;
  if (!AddSize(ciphertext_size, sizeof(std::uint8_t) + Uid::kSize) ||
      !AddSize(ciphertext_size, *payload_length) ||
      !AddSize(ciphertext_size, *overhead)) {
    return std::nullopt;
  }
  auto const ciphertext_length = PackedLengthBytes(ciphertext_size);
  if (!ciphertext_length) {
    return std::nullopt;
  }
  auto encoded_size = ciphertext_size;
  if (!AddSize(encoded_size, sizeof(std::uint8_t) + Uid::kSize) ||
      !AddSize(encoded_size, *ciphertext_length)) {
    return std::nullopt;
  }
  return encoded_size;
}

}  // namespace p2p_stream_write_budget_internal

std::size_t EncodedP2pSendMessageDatagramBytesForInnerPayload(
    std::size_t inner_payload_bytes) {
  return p2p_stream_write_budget_internal::EncodedBytes(inner_payload_bytes)
      .value_or(std::numeric_limits<std::size_t>::max());
}

std::size_t MaxP2pStreamWritePayloadBytesForDatagramBudget(
    std::size_t datagram_budget_bytes) {
  if (datagram_budget_bytes == 0) {
    return 0;
  }
  std::size_t lo = 0;
  std::size_t hi = datagram_budget_bytes;
  while (lo < hi) {
    auto const distance = hi - lo;
    auto const mid = lo + distance / 2 + distance % 2;
    auto const encoded = p2p_stream_write_budget_internal::EncodedBytes(mid);
    if (encoded && *encoded <= datagram_budget_bytes) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

std::size_t P2pStreamWritePayloadLimitFromChannelMax(
    std::size_t channel_max_element_size) {
  if (channel_max_element_size > kP2pStreamWriteBudgetDatagramSizingMax) {
    return channel_max_element_size;
  }
  return MaxP2pStreamWritePayloadBytesForDatagramBudget(channel_max_element_size);
}
}  // namespace ae
