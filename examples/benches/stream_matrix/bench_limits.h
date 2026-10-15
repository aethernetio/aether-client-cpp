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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_LIMITS_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_LIMITS_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aether/config.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"

namespace ae::stream_matrix_bench {

inline constexpr std::size_t kBenchmarkUdpPayloadBudgetBytes = 1200;
inline constexpr std::size_t kServerMsgMaxBytesSourceDefault = 65536;
inline constexpr std::size_t kServerMsgQueueLimitSourceDefault = 1000;
inline constexpr std::size_t kServerMsgTimeLimitSourceDefault = 60;

struct SafeStreamLimitsDoc {
  std::size_t capacity{AE_SAFE_STREAM_CAPACITY};
  SafeStreamConfig config;
  std::size_t max_element_size_reported{};
  std::size_t rec_element_size_reported{};
};

struct PayloadLimitResult {
  std::size_t max_payload_bytes{};
  std::optional<std::string> skip_reason;
};

enum class BenchStreamKind { kP2pStream, kP2pSafeStream };
enum class BenchTransportKind { kUdp, kTcp };

std::vector<std::size_t> DefaultPayloadSweep();
std::vector<double> PeriodsMsForMaxRate(double max_messages_per_sec);

PayloadLimitResult ComputePayloadLimit(BenchStreamKind stream,
                                       BenchTransportKind transport,
                                       StreamInfo const& send_path_info,
                                       SafeStreamLimitsDoc const& safe_doc,
                                       std::size_t envelope_serialized_bytes);

std::string_view StreamKindName(BenchStreamKind kind);
std::string_view TransportKindName(BenchTransportKind kind);

}  // namespace ae::stream_matrix_bench

#endif  // EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_LIMITS_H_
