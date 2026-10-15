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

#include "stream_matrix/bench_udp_budget.h"

#include <algorithm>

#include "stream_matrix/bench_envelope.h"

namespace ae::stream_matrix_bench {
namespace {

// AeMessage { uid, data } body on the AuthorizedApi::SendMessage path (binary
// seri tags + uid bytes + payload length + payload bytes).
inline constexpr std::size_t kAeMessageFixedOverheadBytes = 36;

// Conservative reserve for Æther packet framing + encryption/MAC beyond the
// serialized AuthorizedApi::SendMessage body. IP/UDP headers are excluded.
inline constexpr std::size_t kApiCryptoReserveBytes = 96;

std::size_t EstimatedUdpPayloadBytes(std::size_t bench_envelope_bytes,
                                     StreamInfo const& p2p_send) {
  std::size_t socket_cap = kBenchmarkUdpPayloadBudgetBytes;
  if (p2p_send.max_element_size > 0) {
    socket_cap = std::min(socket_cap, p2p_send.max_element_size);
  }
  (void)socket_cap;
  return kAeMessageFixedOverheadBytes + bench_envelope_bytes +
         kApiCryptoReserveBytes;
}

}  // namespace

UdpBudgetEstimate EstimateUdpBenchEnvelopeBudget(StreamInfo const& p2p_send) {
  UdpBudgetEstimate est;
  est.ae_message_wrap_bytes = kAeMessageFixedOverheadBytes;
  est.api_crypto_reserve_bytes = kApiCryptoReserveBytes;
  std::size_t socket_cap = kBenchmarkUdpPayloadBudgetBytes;
  if (p2p_send.max_element_size > 0) {
    socket_cap = std::min(socket_cap, p2p_send.max_element_size);
  }
  if (socket_cap <= est.ae_message_wrap_bytes + est.api_crypto_reserve_bytes) {
    est.max_bench_envelope_bytes = 0;
    return est;
  }
  est.max_bench_envelope_bytes =
      socket_cap - est.ae_message_wrap_bytes - est.api_crypto_reserve_bytes;
  return est;
}

bool BenchEnvelopeWithinUdpSocketBudget(std::size_t bench_envelope_bytes,
                                        StreamInfo const& p2p_send) {
  auto est = EstimateUdpBenchEnvelopeBudget(p2p_send);
  if (bench_envelope_bytes > est.max_bench_envelope_bytes) {
    return false;
  }
  return EstimatedUdpPayloadBytes(bench_envelope_bytes, p2p_send) <=
         std::min(kBenchmarkUdpPayloadBudgetBytes,
                  p2p_send.max_element_size > 0 ? p2p_send.max_element_size
                                               : kBenchmarkUdpPayloadBudgetBytes);
}

std::size_t MaxBenchEnvelopeBytesForUdp(StreamInfo const& p2p_send) {
  return EstimateUdpBenchEnvelopeBudget(p2p_send).max_bench_envelope_bytes;
}

}  // namespace ae::stream_matrix_bench
