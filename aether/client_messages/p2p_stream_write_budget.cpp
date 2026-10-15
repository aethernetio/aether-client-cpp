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

#include <algorithm>
#include <vector>

#include "aether/config.h"
#include "aether/crypto/key.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/prepared_packet/packet_encoder.h"
#include "aether/prepared_packet/prepared_send_message.h"
#include "aether/types/data_buffer.h"

namespace ae {
namespace {

prepared_packet::PreparedSendMessageBlock SizingPreparedBlock() {
  prepared_packet::PreparedSendMessageBlock block;
  prepared_packet::PreparedSendMessage send_message;
  send_message.sender_ephemeral = Uid{{1}};
  send_message.destination_uid = Uid{{2}};
  send_message.message_left = 1;
  send_message.next_nonce.Init();
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  send_message.client_to_server_key = Key{SodiumChacha20Poly1305Key{}};
#elif AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  send_message.client_to_server_key = Key{HydrogenSecretBoxKey{}};
#endif
  block.Retain(std::move(send_message));
  return block;
}

std::size_t EncodedDatagramBytesForInnerPayload(DataBuffer inner) {
  auto block = SizingPreparedBlock();
  DataBuffer packet;
  auto result = prepared_packet::EncodePacket(block, std::move(inner), packet);
  if (!result.IsOk()) {
    return std::numeric_limits<std::size_t>::max();
  }
  return result.value();
}

}  // namespace

std::size_t EncodedP2pSendMessageDatagramBytesForInnerPayload(
    std::size_t inner_payload_bytes) {
  std::vector<std::uint8_t> bytes(inner_payload_bytes, 0xA5);
  return EncodedDatagramBytesForInnerPayload(DataBuffer{bytes});
}

std::size_t MaxP2pStreamWritePayloadBytesForDatagramBudget(
    std::size_t datagram_budget_bytes) {
  if (datagram_budget_bytes == 0) {
    return 0;
  }
  std::size_t lo = 0;
  std::size_t hi = datagram_budget_bytes;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo + 1) / 2;
    if (EncodedP2pSendMessageDatagramBytesForInnerPayload(mid) <=
        datagram_budget_bytes) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

}  // namespace ae
