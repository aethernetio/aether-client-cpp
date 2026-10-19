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
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include "aether/stream_matrix_udp_audit.h"

#include "stream_matrix/bench_clock.h"

#include "stream_matrix/bench_envelope.h"

namespace ae::stream_matrix_bench {
namespace {

inline constexpr std::size_t kAeMessageFixedOverheadBytes = 36;
inline constexpr std::size_t kApiCryptoReserveBytes = 96;

std::atomic<std::size_t> g_max_tx{0};
std::atomic<std::size_t> g_max_rx{0};
std::atomic<std::uint64_t> g_tx_violations{0};
std::atomic<std::uint64_t> g_rx_violations{0};
std::atomic<bool> g_socket_observed{false};
std::atomic<std::uint64_t> g_recv_error_events{0};
std::atomic<std::uint64_t> g_recv_emsgsize_events{0};
std::mutex g_trace_mu;
std::unique_ptr<std::ofstream> g_trace_file;
std::atomic<std::uint64_t> g_trace_seq{0};
BenchInstant g_trace_t0{};

static void TraceLine(std::string const& line) {
  std::lock_guard lock{g_trace_mu};
  if (!g_trace_file || !g_trace_file->is_open()) {
    return;
  }
  auto const ms = std::chrono::duration<double, std::milli>(BenchNow() - g_trace_t0)
                      .count();
  *g_trace_file << g_trace_seq.fetch_add(1) << ',' << ms << ',' << line << '\n';
  g_trace_file->flush();
}

void AuditTx(std::size_t payload_bytes) {
  TraceLine("dir=tx,payload_bytes=" + std::to_string(payload_bytes));
  g_socket_observed.store(true);
  auto prev = g_max_tx.load();
  while (payload_bytes > prev &&
         !g_max_tx.compare_exchange_weak(prev, payload_bytes)) {
  }
  if (payload_bytes > kBenchmarkUdpPayloadBudgetBytes) {
    g_tx_violations.fetch_add(1);
  }
}

void AuditRx(std::size_t payload_bytes) {
  TraceLine("dir=rx,payload_bytes=" + std::to_string(payload_bytes));
  g_socket_observed.store(true);
  auto prev = g_max_rx.load();
  while (payload_bytes > prev &&
         !g_max_rx.compare_exchange_weak(prev, payload_bytes)) {
  }
  if (payload_bytes > kBenchmarkUdpPayloadBudgetBytes) {
    g_rx_violations.fetch_add(1);
  }
}

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

void AuditRecvError(int wsa_error, std::size_t recv_buffer_bytes) {
  g_recv_error_events.fetch_add(1);
  if (wsa_error == 10040) {  // WSAEMSGSIZE
    g_recv_emsgsize_events.fetch_add(1);
  }
  TraceLine("dir=recv_error,wsa=" + std::to_string(wsa_error) +
            ",recv_buffer_bytes=" + std::to_string(recv_buffer_bytes));
}

}  // namespace

UdpBudgetEstimate EstimateUdpBenchEnvelopeBudget(StreamInfo const& p2p_send) {
  UdpBudgetEstimate est;
  est.envelope_estimate_only = true;
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

bool ApplyUdpWholeMessageEnvelopeEstimate(BenchStreamKind stream,
                                          BenchTransportKind transport) {
  return transport == BenchTransportKind::kUdp &&
         stream == BenchStreamKind::kP2pStream;
}

std::string_view ClassifyUdpMeasureOutcome(
    BenchStreamKind stream, BenchTransportKind transport,
    std::string_view prior_outcome, std::size_t serialized_measure_bytes,
    StreamInfo const& p2p_send, UdpSocketAuditSnapshot const& audit) {
  if (!prior_outcome.empty()) {
    return prior_outcome;
  }
  if (transport != BenchTransportKind::kUdp) {
    return {};
  }
  if (audit.tx_violations != 0 || audit.rx_violations != 0) {
    return "UDP_SOCKET_BUDGET_VIOLATION";
  }
  if (ApplyUdpWholeMessageEnvelopeEstimate(stream, transport) &&
      !BenchEnvelopeWithinUdpSocketBudget(serialized_measure_bytes,
                                          p2p_send)) {
    return "UDP_ENVELOPE_ESTIMATE_EXCEEDED";
  }
  return {};
}

void InstallUdpSocketPayloadAudit() {
  ae::SetStreamMatrixUdpPayloadAudit(&AuditTx, &AuditRx);
  ae::SetStreamMatrixUdpRecvErrorAudit(&AuditRecvError);
}

void ResetUdpSocketPayloadAudit() {
  g_max_tx.store(0);
  g_max_rx.store(0);
  g_tx_violations.store(0);
  g_rx_violations.store(0);
  g_socket_observed.store(false);
  g_recv_error_events.store(0);
  g_recv_emsgsize_events.store(0);
}

void BeginUdpDatagramTrace(std::filesystem::path const& path) {
  std::lock_guard lock{g_trace_mu};
  g_trace_file = std::make_unique<std::ofstream>(path, std::ios::trunc);
  g_trace_seq.store(0);
  g_trace_t0 = BenchNow();
  if (g_trace_file && g_trace_file->is_open()) {
    *g_trace_file << "seq,elapsed_ms,event\n";
  }
}

void EndUdpDatagramTrace() {
  std::lock_guard lock{g_trace_mu};
  g_trace_file.reset();
}

UdpSocketAuditSnapshot UdpSocketPayloadAuditSnapshot() {
  return UdpSocketAuditSnapshot{
      .max_tx_payload_bytes = g_max_tx.load(),
      .max_rx_payload_bytes = g_max_rx.load(),
      .tx_violations = g_tx_violations.load(),
      .rx_violations = g_rx_violations.load(),
      .recv_error_events = g_recv_error_events.load(),
      .recv_emsgsize_events = g_recv_emsgsize_events.load(),
      .socket_observed = g_socket_observed.load(),
      .safestream_datagram_unverified = true,
  };
}

void WriteUdpAuditReport(std::ostream& out, UdpSocketAuditSnapshot const& snap) {
  out << "udp_socket_observed=" << snap.socket_observed
      << " max_tx_payload=" << snap.max_tx_payload_bytes
      << " max_rx_payload=" << snap.max_rx_payload_bytes
      << " tx_violations=" << snap.tx_violations
      << " rx_violations=" << snap.rx_violations
      << " safestream_per_datagram_unverified="
      << snap.safestream_datagram_unverified << '\n';
}

}  // namespace ae::stream_matrix_bench
