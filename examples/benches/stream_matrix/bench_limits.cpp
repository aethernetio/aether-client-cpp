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

#include "stream_matrix/bench_limits.h"

#include <algorithm>
#include <cmath>

namespace ae::stream_matrix_bench {

std::vector<std::size_t> DefaultPayloadSweep() {
  std::vector<std::size_t> sizes;
  for (std::size_t s = 100; s <= 51200; s *= 2) {
    sizes.push_back(s);
  }
  return sizes;
}

std::vector<double> PeriodsMsForMaxRate(double max_messages_per_sec) {
  std::vector<double> periods_ms{1000.0, 500.0, 250.0, 125.0, 62.5, 31.25,
                                 15.625, 7.8125};
  if (max_messages_per_sec <= 0.0) {
    return periods_ms;
  }
  periods_ms.erase(
      std::remove_if(periods_ms.begin(), periods_ms.end(),
                     [&](double period_ms) {
                       return (1000.0 / period_ms) > max_messages_per_sec;
                     }),
      periods_ms.end());
  return periods_ms;
}

static PayloadLimitResult LimitFromSerialized(std::size_t max_serialized,
                                              std::size_t envelope_bytes,
                                              std::string_view reason) {
  if (max_serialized <= envelope_bytes) {
    return PayloadLimitResult{
        .max_payload_bytes = 0,
        .skip_reason = std::string{reason},
    };
  }
  return PayloadLimitResult{.max_payload_bytes = max_serialized - envelope_bytes};
}

PayloadLimitResult ComputePayloadLimit(BenchStreamKind stream,
                                       BenchTransportKind transport,
                                       StreamInfo const& send_path_info,
                                       SafeStreamLimitsDoc const& safe_doc,
                                       std::size_t envelope_serialized_bytes) {
  std::size_t caps[4];
  std::size_t cap_count = 0;

  auto push_cap = [&](std::size_t v) {
    if (v > 0) {
      caps[cap_count++] = v;
    }
  };

  push_cap(kServerMsgMaxBytesSourceDefault);

  if (stream == BenchStreamKind::kP2pStream) {
    push_cap(send_path_info.max_element_size);
    push_cap(send_path_info.rec_element_size);
    if (transport == BenchTransportKind::kUdp) {
      push_cap(kBenchmarkUdpPayloadBudgetBytes);
    }
  } else {
    push_cap(safe_doc.max_element_size_reported);
    if (transport == BenchTransportKind::kUdp) {
      push_cap(AE_SAFE_STREAM_CAPACITY);
      push_cap(send_path_info.rec_element_size);
    }
  }

  if (cap_count == 0) {
    return PayloadLimitResult{.max_payload_bytes = 0,
                              .skip_reason = "no_stream_info"};
  }
  std::size_t min_cap = *std::min_element(caps, caps + cap_count);
  return LimitFromSerialized(min_cap, envelope_serialized_bytes,
                             "SIZE_LIMIT");
}

std::string_view StreamKindName(BenchStreamKind kind) {
  switch (kind) {
    case BenchStreamKind::kP2pStream:
      return "P2pStream";
    case BenchStreamKind::kP2pSafeStream:
      return "P2pSafeStream";
  }
  return "unknown";
}

std::string_view TransportKindName(BenchTransportKind kind) {
  switch (kind) {
    case BenchTransportKind::kUdp:
      return "UDP";
    case BenchTransportKind::kTcp:
      return "TCP";
  }
  return "unknown";
}

}  // namespace ae::stream_matrix_bench
