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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_UDP_BUDGET_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_UDP_BUDGET_H_

#include <cstddef>

#include "aether/stream_api/istream.h"

#include "stream_matrix/bench_limits.h"

namespace ae::stream_matrix_bench {

struct UdpBudgetEstimate {
  std::size_t ae_message_wrap_bytes{};
  std::size_t api_crypto_reserve_bytes{};
  std::size_t max_bench_envelope_bytes{};
};

UdpBudgetEstimate EstimateUdpBenchEnvelopeBudget(StreamInfo const& p2p_send);

bool BenchEnvelopeWithinUdpSocketBudget(std::size_t bench_envelope_bytes,
                                        StreamInfo const& p2p_send);

std::size_t MaxBenchEnvelopeBytesForUdp(StreamInfo const& p2p_send);

}  // namespace ae::stream_matrix_bench

#endif
