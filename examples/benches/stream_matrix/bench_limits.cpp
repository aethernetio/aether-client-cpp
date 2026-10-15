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

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_udp_budget.h"

namespace ae::stream_matrix_bench {

std::vector<std::size_t> PayloadSweepFrom100() {
  std::vector<std::size_t> sizes;
  for (std::size_t s = 100; s <= 51200; s *= 2) {
    sizes.push_back(s);
  }
  return sizes;
}

std::vector<double> PeriodsMsFirstSecond() { return {1000.0}; }

std::vector<double> PeriodsMsFast(double max_messages_per_sec) {
  std::vector<double> periods_ms;
  for (double period_ms = 500.0; period_ms >= 1.0; period_ms *= 0.5) {
    if (max_messages_per_sec > 0.0 &&
        (1000.0 / period_ms) > max_messages_per_sec) {
      continue;
    }
    periods_ms.push_back(period_ms);
  }
  return periods_ms;
}

std::vector<std::size_t> BuildSizeMatrix(std::size_t max_payload,
                                         bool include_boundary) {
  auto sizes = PayloadSweepFrom100();
  sizes.erase(std::remove_if(sizes.begin(), sizes.end(),
                             [&](std::size_t s) { return s > max_payload; }),
              sizes.end());
  if (include_boundary && max_payload > 0 &&
      (sizes.empty() || sizes.back() != max_payload)) {
    sizes.push_back(max_payload);
  }
  return sizes;
}

std::size_t SerializedMeasureBytes(std::size_t payload_bytes,
                                   std::uint64_t run_id, std::uint64_t point_id,
                                   std::uint64_t sequence) {
  auto payload = MakePayloadPattern(payload_bytes);
  return SerializeEnvelope(BenchEnvelope{
                               .kind = static_cast<std::uint8_t>(EnvelopeKind::kMeasure),
                               .run_id = run_id,
                               .point_id = point_id,
                               .sequence = sequence,
                               .payload = std::move(payload),
                           })
      .size();
}

static PayloadLimitResult Skip(std::string_view reason) {
  return PayloadLimitResult{.max_payload_bytes = 0, .skip_reason = std::string{reason}};
}

PayloadLimitResult ComputePayloadLimit(BenchStreamKind stream,
                                       BenchTransportKind transport,
                                       StreamInfo const& p2p_send_info,
                                       SafeStreamLimitsDoc const& safe_doc,
                                       std::size_t envelope_overhead_bytes) {
  std::size_t max_serialized = kServerMsgMaxBytesSourceDefault;

  if (stream == BenchStreamKind::kP2pStream) {
    if (transport == BenchTransportKind::kUdp) {
      max_serialized = MaxBenchEnvelopeBytesForUdp(p2p_send_info);
    } else if (p2p_send_info.max_element_size > 0) {
      max_serialized =
          std::min(max_serialized, p2p_send_info.max_element_size);
    }
  } else {
    if (safe_doc.max_element_size_reported > 0) {
      max_serialized =
          std::min(max_serialized, safe_doc.max_element_size_reported);
    }
  }

  if (max_serialized <= envelope_overhead_bytes) {
    return Skip("SIZE_LIMIT");
  }

  std::size_t lo = 0;
  std::size_t hi = max_serialized;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo + 1) / 2;
    auto ser = SerializedMeasureBytes(mid, 1, 1, 1);
    bool ok = ser <= max_serialized;
    if (transport == BenchTransportKind::kUdp &&
        stream == BenchStreamKind::kP2pStream) {
      ok = ok && BenchEnvelopeWithinUdpSocketBudget(ser, p2p_send_info);
    }
    if (ok) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  return PayloadLimitResult{
      .max_payload_bytes = lo,
      .max_serialized_envelope_bytes = SerializedMeasureBytes(lo, 1, 1, 1),
  };
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
