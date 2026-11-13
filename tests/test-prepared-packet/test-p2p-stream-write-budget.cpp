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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <unity.h>

#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/config.h"
#include "aether/crypto/key.h"
#include "aether/prepared_packet/packet_encoder.h"
#include "aether/prepared_packet/prepared_send_message.h"
#include "aether/types/data_buffer.h"
#include "aether/types/uid.h"

namespace ae::test_p2p_stream_write_budget {

#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
std::size_t ActualEncodedSize(std::size_t payload_size) {
  prepared_packet::PreparedSendMessageBlock block;
  prepared_packet::PreparedSendMessage message;
  message.sender_ephemeral = Uid{{1}};
  message.destination_uid = Uid{{2}};
  message.message_left = 1;
  message.next_nonce.Init();
#  if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305
  message.client_to_server_key = Key{SodiumChacha20Poly1305Key{}};
#  else
  message.client_to_server_key = Key{HydrogenSecretBoxKey{}};
#  endif
  block.Retain(std::move(message));
  DataBuffer packet;
  auto const result = prepared_packet::EncodePacket(
      block, DataBuffer(payload_size, 0xA5), packet);
  TEST_ASSERT_TRUE(result.IsOk());
  return packet.size();
}

void test_ExactSizeMatchesRealEncoderAtLengthBoundaries() {
  // Exercise both sized buffers around every practical PackedSize tier. The
  // ciphertext prefix crosses its boundary before the inner payload prefix.
  constexpr std::size_t sizes[]{0,     1,       127,     128,    187,  188,
                                189,   249,     250,     251,    1199, 1200,
                                1450,  1500,    1513,    1514,   1515, 65535,
                                65536, 1049833, 1049834, 1049835};
  for (auto const size : sizes) {
    TEST_ASSERT_TRUE(ActualEncodedSize(size) ==
                     EncodedP2pSendMessageDatagramBytesForInnerPayload(size));
  }
  // Includes profile-dependent outer-ciphertext length transitions.
  for (std::size_t size = 0; size < 2048; ++size) {
    TEST_ASSERT_TRUE(ActualEncodedSize(size) ==
                     EncodedP2pSendMessageDatagramBytesForInnerPayload(size));
  }
}

void test_MaximumPayloadFitsRealEncoderAndNextPayloadDoesNot() {
  constexpr std::size_t budgets[]{0,   64,  79,   80,   81,   127,  128,  249,
                                  250, 251, 1199, 1200, 1500, 1514, 65536};
  auto const empty_size = ActualEncodedSize(0);
  for (auto const budget : budgets) {
    auto const payload = MaxP2pStreamWritePayloadBytesForDatagramBudget(budget);
    if (budget < empty_size) {
      TEST_ASSERT_TRUE(payload == 0);
      continue;
    }
    TEST_ASSERT_TRUE(ActualEncodedSize(payload) <= budget);
    TEST_ASSERT_TRUE(ActualEncodedSize(payload + 1) > budget);
  }
}

void test_TcpMaximumBudgetDoesNotAllocateGigabytePayloads() {
  auto const budget =
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
  auto const payload = MaxP2pStreamWritePayloadBytesForDatagramBudget(budget);
  TEST_ASSERT_TRUE(payload > budget / 2);
  TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(payload) <=
                   budget);
  if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t)) {
    TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(
                         payload + 1) > budget);
  } else {
    // The unrepresentable encoded size saturates at size_t max on 32-bit.
    TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(
                         payload + 1) == budget);
  }
}

void test_SizeOverflowDoesNotWrapOrBreakBinarySearch() {
  auto const limit = std::numeric_limits<std::size_t>::max();
  TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(limit) ==
                   limit);
  auto const payload = MaxP2pStreamWritePayloadBytesForDatagramBudget(limit);
  TEST_ASSERT_TRUE(payload > 0);
  TEST_ASSERT_TRUE(payload < limit);
  // This value is representable, whereas values near size_t max saturate.
  TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(payload) >
                   payload);
}

#else
void test_UnsupportedCryptoDoesNotClaimPayloadFits() {
  auto const limit = std::numeric_limits<std::size_t>::max();
  TEST_ASSERT_TRUE(EncodedP2pSendMessageDatagramBytesForInnerPayload(0) ==
                   limit);
  TEST_ASSERT_TRUE(MaxP2pStreamWritePayloadBytesForDatagramBudget(limit) == 0);
}
#endif

}  // namespace ae::test_p2p_stream_write_budget

int test_p2p_stream_write_budget() {
  UNITY_BEGIN();
#if AE_CRYPTO_SYNC == AE_CHACHA20_POLY1305 || \
    AE_CRYPTO_SYNC == AE_HYDRO_CRYPTO_SK
  RUN_TEST(ae::test_p2p_stream_write_budget::
               test_ExactSizeMatchesRealEncoderAtLengthBoundaries);
  RUN_TEST(ae::test_p2p_stream_write_budget::
               test_MaximumPayloadFitsRealEncoderAndNextPayloadDoesNot);
  RUN_TEST(ae::test_p2p_stream_write_budget::
               test_TcpMaximumBudgetDoesNotAllocateGigabytePayloads);
  RUN_TEST(ae::test_p2p_stream_write_budget::
               test_SizeOverflowDoesNotWrapOrBreakBinarySearch);
#else
  RUN_TEST(ae::test_p2p_stream_write_budget::
               test_UnsupportedCryptoDoesNotClaimPayloadFits);
#endif
  return UNITY_END();
}
