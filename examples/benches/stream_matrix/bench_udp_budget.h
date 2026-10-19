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
#include <cstdint>
#include <filesystem>
#include <ostream>

#include "aether/stream_api/istream.h"

#include "stream_matrix/bench_limits.h"

namespace ae::stream_matrix_bench {

struct UdpBudgetEstimate {
  std::size_t ae_message_wrap_bytes{};
  std::size_t api_crypto_reserve_bytes{};
  std::size_t max_bench_envelope_bytes{};
  bool envelope_estimate_only{true};
};

enum class UdpBudgetStatus {
  kCompliant,
  kViolated,
  kUnverified,
};

struct UdpSocketAuditSnapshot {
  std::size_t max_tx_payload_bytes{};
  std::size_t max_rx_payload_bytes{};
  std::uint64_t tx_violations{};
  std::uint64_t rx_violations{};
  std::uint64_t recv_error_events{};
  std::uint64_t recv_emsgsize_events{};
  std::uint64_t recv_emsgsize_budget_lower_bound{};
  std::uint64_t recv_emsgsize_buffer_only{};
  std::uint64_t recv_other_errors{};
  std::size_t recv_buffer_bytes{};
  bool tx_observed{false};
  bool rx_observed{false};
  bool safestream_datagram_unverified{true};
};

struct UdpBudgetVerdict {
  UdpBudgetStatus status{UdpBudgetStatus::kUnverified};
  bool tx_observed{false};
  bool rx_observed{false};
  bool tx_compliant{false};
  bool rx_compliant{false};
  std::uint64_t measured_tx_oversize{};
  std::uint64_t measured_rx_oversize{};
  std::uint64_t emsgsize_budget_lower_bound{};
  std::uint64_t recv_other_errors{};
  bool budget_ok() const { return status == UdpBudgetStatus::kCompliant; }
};

UdpBudgetVerdict EvaluateUdpBudget(UdpSocketAuditSnapshot const& snap);

char const* UdpBudgetStatusName(UdpBudgetStatus status);

// Human-readable UDP budget outcome suffix; empty when COMPLIANT.
std::string_view ClassifyUdpBudgetOutcome(UdpBudgetVerdict const& verdict);

UdpBudgetEstimate EstimateUdpBenchEnvelopeBudget(StreamInfo const& p2p_send);

bool BenchEnvelopeWithinUdpSocketBudget(std::size_t bench_envelope_bytes,
                                        StreamInfo const& p2p_send);

bool ApplyUdpWholeMessageEnvelopeEstimate(BenchStreamKind stream,
                                          BenchTransportKind transport);

// After socket audit: UDP_SOCKET_BUDGET_VIOLATION, UDP_ENVELOPE_ESTIMATE_EXCEEDED
// (P2pStream only), or empty when no UDP-specific outcome applies.
std::string_view ClassifyUdpMeasureOutcome(
    BenchStreamKind stream, BenchTransportKind transport,
    std::string_view prior_outcome, std::size_t serialized_measure_bytes,
    StreamInfo const& p2p_send, UdpSocketAuditSnapshot const& audit);

std::size_t MaxBenchEnvelopeBytesForUdp(StreamInfo const& p2p_send);

void InstallUdpSocketPayloadAudit();
void ResetUdpSocketPayloadAudit();
struct UdpDatagramTraceOptions {
  bool verbose{false};
  std::uint64_t transfer_id{};
};

void BeginUdpDatagramTrace(std::filesystem::path const& path,
                           UdpDatagramTraceOptions options = {});
void EndUdpDatagramTrace();
void SetUdpDatagramTraceSide(char side);
void InstallUdpDatagramBenchTraceHooks();
void ClearUdpDatagramBenchTraceHooks();
UdpSocketAuditSnapshot UdpSocketPayloadAuditSnapshot();
void WriteUdpAuditReport(std::ostream& out, UdpSocketAuditSnapshot const& snap);

}  // namespace ae::stream_matrix_bench

#endif
