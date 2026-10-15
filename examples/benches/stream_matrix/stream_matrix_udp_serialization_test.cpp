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

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/safe_stream/safe_stream_wire_budget.h"

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_udp_budget.h"

namespace ae::stream_matrix_bench {
namespace {

void Expect(bool ok, std::string_view msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    std::exit(1);
  }
}

void test_p2p_inner_payload_fits_udp_datagram_budget() {
  auto const budget = kBenchmarkUdpPayloadBudgetBytes;
  auto const max_inner =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(budget);
  Expect(max_inner > 0, "P2p inner payload max must be positive at 1200 B UDP");
  auto const datagram =
      EncodedP2pSendMessageDatagramBytesForInnerPayload(max_inner);
  Expect(datagram <= budget, "max inner payload must encode within UDP budget");
  auto const over =
      EncodedP2pSendMessageDatagramBytesForInnerPayload(max_inner + 1);
  Expect(over > budget, "max inner payload + 1 must exceed UDP budget");
}

void test_safestream_send_fragment_fits_p2p_inner_budget() {
  auto const budget = kBenchmarkUdpPayloadBudgetBytes;
  auto const max_inner =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(budget);
  auto const max_data =
      MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(max_inner);
  Expect(max_data > 0, "SafeStream send data max must be positive");
  auto const packed = PackedSafeStreamSendBytes(max_data);
  Expect(packed <= max_inner,
         "SafeStream packed Send must fit P2p inner payload budget");
  auto const datagram =
      EncodedP2pSendMessageDatagramBytesForInnerPayload(packed);
  Expect(datagram <= budget,
         "SafeStream fragment must encode to UDP datagram within budget");
}

void test_no_double_subtraction_at_boundary() {
  auto const budget = kBenchmarkUdpPayloadBudgetBytes;
  auto const max_inner =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(budget);
  auto const max_data =
      MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(max_inner);
  auto const packed = PackedSafeStreamSendBytes(max_data);
  Expect(
      MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(budget) > max_data,
      "SafeStream must receive P2p inner payload budget, not raw datagram budget");
  Expect(packed <= max_inner,
         "boundary packed size must match downstream max_element_size semantics");
}

void test_small_datagram_budget_no_overflow() {
  auto const tiny = std::size_t{64};
  auto const max_inner = MaxP2pStreamWritePayloadBytesForDatagramBudget(tiny);
  auto const max_data =
      MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(max_inner);
  if (max_inner > 0) {
    Expect(EncodedP2pSendMessageDatagramBytesForInnerPayload(max_inner) <= tiny,
           "small budget inner payload must fit");
    if (max_data > 0) {
      Expect(PackedSafeStreamSendBytes(max_data) <= max_inner,
             "small budget SafeStream fragment must fit inner payload");
    } else {
      Expect(PackedSafeStreamSendBytes(0) > max_inner,
             "zero SafeStream data when inner budget cannot fit Send header");
    }
  } else {
    Expect(max_data == 0, "zero inner budget must yield zero SafeStream data");
  }
}

void test_large_user_messages_remain_valid() {
  Expect(MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(65536) > 1600,
         "large downstream budget must allow 1600 B SafeStream send fragments");
}

void test_tcp_channel_max_skips_datagram_sizing() {
  auto const tcp_like_max = static_cast<std::size_t>(65536);
  Expect(P2pStreamWritePayloadLimitFromChannelMax(tcp_like_max) == tcp_like_max,
         "large channel max must pass through without datagram sizing");
  auto const huge = static_cast<std::size_t>(UINT32_MAX);
  Expect(P2pStreamWritePayloadLimitFromChannelMax(huge) == huge,
         "UINT32_MAX channel max must not allocate probe payloads");
}

void test_zero_budget_yields_zero_limits() {
  Expect(MaxP2pStreamWritePayloadBytesForDatagramBudget(0) == 0,
         "zero datagram budget");
  Expect(P2pStreamWritePayloadLimitFromChannelMax(0) == 0, "zero channel max");
  Expect(MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(0) == 0,
         "zero downstream max");
}

}  // namespace
}  // namespace ae::stream_matrix_bench

int main() {
  ae::stream_matrix_bench::test_p2p_inner_payload_fits_udp_datagram_budget();
  ae::stream_matrix_bench::test_safestream_send_fragment_fits_p2p_inner_budget();
  ae::stream_matrix_bench::test_no_double_subtraction_at_boundary();
  ae::stream_matrix_bench::test_small_datagram_budget_no_overflow();
  ae::stream_matrix_bench::test_large_user_messages_remain_valid();
  ae::stream_matrix_bench::test_tcp_channel_max_skips_datagram_sizing();
  ae::stream_matrix_bench::test_zero_budget_yields_zero_limits();
  std::cout << "stream_matrix_udp_serialization_test: OK\n";
  return 0;
}
