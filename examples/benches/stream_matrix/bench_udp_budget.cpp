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

#include "aether/config.h"
#include "aether/stream_matrix_bench_trace.h"
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
std::atomic<bool> g_tx_observed{false};
std::atomic<bool> g_rx_observed{false};
std::atomic<std::uint64_t> g_recv_error_events{0};
std::atomic<std::uint64_t> g_recv_emsgsize_events{0};
std::atomic<std::uint64_t> g_recv_emsgsize_budget_lower_bound{0};
std::atomic<std::uint64_t> g_recv_emsgsize_buffer_only{0};
std::atomic<std::size_t> g_recv_buffer_bytes{AE_WIN_UDP_RECV_BUFFER_BYTES};
std::mutex g_trace_mu;
std::unique_ptr<std::ofstream> g_trace_file;
std::atomic<std::uint64_t> g_trace_seq{0};
BenchInstant g_trace_t0{};
UdpDatagramTraceOptions g_trace_options{};
thread_local char g_trace_side{'U'};
std::atomic<std::uint64_t> g_local_event_id{0};

static std::uint64_t NextLocalSocketEventId() {
  return g_local_event_id.fetch_add(1);
}

static void TraceLine(std::string const& line, bool force = false) {
  std::lock_guard lock{g_trace_mu};
  if (!g_trace_file || !g_trace_file->is_open()) {
    return;
  }
  if (!g_trace_options.verbose && !force) {
    return;
  }
  auto const ms = std::chrono::duration<double, std::milli>(BenchNow() - g_trace_t0)
                      .count();
  *g_trace_file << g_trace_seq.fetch_add(1) << ',' << ms << ',' << line << '\n';
  if (g_trace_options.verbose) {
    g_trace_file->flush();
  }
}

void AuditTx(ae::StreamMatrixUdpPayloadEvent const& ev) {
  auto const id = NextLocalSocketEventId();
  TraceLine("kind=socket,dir=tx,side=" + std::string(1, g_trace_side) +
            ",socket_id=" + std::to_string(ev.socket_id) +
            ",endpoint=" + (ev.endpoint ? ev.endpoint : "") +
            ",transfer_id=" + std::to_string(g_trace_options.transfer_id) +
            ",local_event_id=" + std::to_string(id) +
            ",payload_bytes=" + std::to_string(ev.payload_bytes));
  g_tx_observed.store(true);
  auto const payload_bytes = ev.payload_bytes;
  auto prev = g_max_tx.load();
  while (payload_bytes > prev &&
         !g_max_tx.compare_exchange_weak(prev, payload_bytes)) {
  }
  if (payload_bytes > kBenchmarkUdpPayloadBudgetBytes) {
    g_tx_violations.fetch_add(1);
  }
}

void AuditRx(ae::StreamMatrixUdpPayloadEvent const& ev) {
  auto const id = NextLocalSocketEventId();
  TraceLine("kind=socket,dir=rx,side=" + std::string(1, g_trace_side) +
            ",socket_id=" + std::to_string(ev.socket_id) +
            ",endpoint=" + (ev.endpoint ? ev.endpoint : "") +
            ",transfer_id=" + std::to_string(g_trace_options.transfer_id) +
            ",local_event_id=" + std::to_string(id) +
            ",payload_bytes=" + std::to_string(ev.payload_bytes));
  g_rx_observed.store(true);
  auto const payload_bytes = ev.payload_bytes;
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

void AuditRecvError(ae::StreamMatrixUdpRecvErrorEvent const& ev) {
  g_recv_error_events.fetch_add(1);
  g_rx_observed.store(true);
  g_recv_buffer_bytes.store(ev.recv_buffer_bytes);
  if (ev.wsa_error == 10040) {  // WSAEMSGSIZE
    g_recv_emsgsize_events.fetch_add(1);
    if (ev.recv_buffer_bytes >= kBenchmarkUdpPayloadBudgetBytes) {
      g_recv_emsgsize_budget_lower_bound.fetch_add(1);
    } else {
      g_recv_emsgsize_buffer_only.fetch_add(1);
    }
    TraceLine("kind=recv_error,dir=rx,side=" + std::string(1, g_trace_side) +
                  ",socket_id=" + std::to_string(ev.socket_id) +
                  ",endpoint=" + (ev.endpoint ? ev.endpoint : "") +
                  ",wsa=" + std::to_string(ev.wsa_error) +
                  ",recv_buffer_bytes=" + std::to_string(ev.recv_buffer_bytes) +
                  ",datagram_lower_bound_gt=" +
                  std::to_string(ev.recv_buffer_bytes),
              true);
  } else {
    TraceLine("kind=recv_error,dir=rx,side=" + std::string(1, g_trace_side) +
                  ",socket_id=" + std::to_string(ev.socket_id) +
                  ",endpoint=" + (ev.endpoint ? ev.endpoint : "") +
                  ",wsa=" + std::to_string(ev.wsa_error) +
                  ",recv_buffer_bytes=" + std::to_string(ev.recv_buffer_bytes),
              true);
  }
}

void BenchTraceSafeStreamSend(StreamMatrixSafeStreamSendTrace const& t) {
  TraceLine("kind=safestream_send,side=" + std::string(1, t.side) +
            ",transfer_id=" + std::to_string(g_trace_options.transfer_id) +
            ",correlation_id=" + std::to_string(t.correlation_id) +
            ",begin_offset=" + std::to_string(t.begin_offset) +
            ",delta_offset=" + std::to_string(t.delta_offset) +
            ",repeat_count=" + std::to_string(t.repeat_count) +
            ",reset=" + std::to_string(t.reset ? 1 : 0) +
            ",data_bytes=" + std::to_string(t.data_bytes));
}

void BenchTraceSafeStreamAck(StreamMatrixSafeStreamAckTrace const& t) {
  char const* role = t.role == StreamMatrixSafeStreamAckRole::kTx ? "ack_tx" : "ack_rx";
  TraceLine(std::string("kind=safestream_") + role + ",side=" +
            std::string(1, t.side) + ",transfer_id=" +
            std::to_string(g_trace_options.transfer_id) +
            ",ack_offset=" + std::to_string(t.ack_offset));
}

void BenchTraceSafeStreamRecv(StreamMatrixSafeStreamRecvTrace const& t) {
  TraceLine("kind=safestream_recv,side=" + std::string(1, t.side) +
            ",transfer_id=" + std::to_string(g_trace_options.transfer_id) +
            ",begin_offset=" + std::to_string(t.begin_offset) +
            ",delta_offset=" + std::to_string(t.delta_offset) +
            ",repeat_count=" + std::to_string(t.repeat_count) +
            ",reset=" + std::to_string(t.reset ? 1 : 0) +
            ",data_bytes=" + std::to_string(t.data_bytes));
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
  auto const budget = EvaluateUdpBudget(audit);
  if (!budget.budget_ok()) {
    auto const out = ClassifyUdpBudgetOutcome(budget);
    if (!out.empty()) {
      return out;
    }
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
  g_tx_observed.store(false);
  g_rx_observed.store(false);
  g_recv_error_events.store(0);
  g_recv_emsgsize_events.store(0);
  g_recv_emsgsize_budget_lower_bound.store(0);
  g_recv_emsgsize_buffer_only.store(0);
  g_recv_buffer_bytes.store(AE_WIN_UDP_RECV_BUFFER_BYTES);
  g_local_event_id.store(0);
}

UdpBudgetVerdict EvaluateUdpBudget(UdpSocketAuditSnapshot const& snap) {
  UdpBudgetVerdict v{};
  v.tx_observed = snap.tx_observed;
  v.rx_observed = snap.rx_observed;
  v.measured_tx_oversize = snap.tx_violations;
  v.measured_rx_oversize = snap.rx_violations;
  v.emsgsize_budget_lower_bound = snap.recv_emsgsize_budget_lower_bound;
  v.recv_other_errors = snap.recv_other_errors;

  bool const tx_oversize =
      v.tx_observed &&
      (v.measured_tx_oversize > 0 ||
       snap.max_tx_payload_bytes > kBenchmarkUdpPayloadBudgetBytes);
  bool const rx_oversize =
      v.rx_observed &&
      (v.measured_rx_oversize > 0 ||
       snap.max_rx_payload_bytes > kBenchmarkUdpPayloadBudgetBytes);
  bool const rx_emsgsize_proves_violation =
      v.emsgsize_budget_lower_bound > 0;

  v.tx_compliant = v.tx_observed && !tx_oversize;
  v.rx_compliant =
      v.rx_observed && !rx_oversize && !rx_emsgsize_proves_violation &&
      v.recv_other_errors == 0;

  if (tx_oversize || rx_oversize || rx_emsgsize_proves_violation) {
    v.status = UdpBudgetStatus::kViolated;
    return v;
  }

  if (v.tx_observed && v.rx_observed && v.tx_compliant && v.rx_compliant) {
    v.status = UdpBudgetStatus::kCompliant;
    return v;
  }

  v.status = UdpBudgetStatus::kUnverified;
  return v;
}

char const* UdpBudgetStatusName(UdpBudgetStatus status) {
  switch (status) {
    case UdpBudgetStatus::kCompliant:
      return "COMPLIANT";
    case UdpBudgetStatus::kViolated:
      return "VIOLATED";
    case UdpBudgetStatus::kUnverified:
      return "UNVERIFIED";
  }
  return "UNVERIFIED";
}

std::string_view ClassifyUdpBudgetOutcome(UdpBudgetVerdict const& verdict) {
  if (verdict.status == UdpBudgetStatus::kCompliant) {
    return {};
  }
  if (verdict.status == UdpBudgetStatus::kUnverified) {
    if (!verdict.tx_observed && !verdict.rx_observed) {
      return "UDP_BUDGET_UNOBSERVED";
    }
    if (verdict.recv_other_errors > 0) {
      return "UDP_BUDGET_RX_INCOMPLETE";
    }
    if (verdict.emsgsize_budget_lower_bound == 0 &&
        verdict.measured_tx_oversize == 0 && verdict.measured_rx_oversize == 0 &&
        (verdict.tx_observed != verdict.rx_observed ||
         !verdict.tx_compliant || !verdict.rx_compliant)) {
      return "UDP_BUDGET_UNVERIFIED";
    }
    return "UDP_BUDGET_UNVERIFIED";
  }
  if (verdict.measured_rx_oversize > 0 || verdict.measured_tx_oversize > 0) {
    return "UDP_BUDGET_MEASURED_OVERSIZE";
  }
  if (verdict.emsgsize_budget_lower_bound > 0) {
    return "UDP_BUDGET_EMSGSIZE_LOWER_BOUND";
  }
  return "UDP_BUDGET_VIOLATION";
}

void BeginUdpDatagramTrace(std::filesystem::path const& path,
                           UdpDatagramTraceOptions options) {
  std::lock_guard lock{g_trace_mu};
  g_trace_options = options;
  g_trace_file = std::make_unique<std::ofstream>(path, std::ios::trunc);
  g_trace_seq.store(0);
  g_trace_t0 = BenchNow();
  if (g_trace_file && g_trace_file->is_open()) {
    *g_trace_file << "seq,elapsed_ms,event\n";
  }
  if (g_trace_options.verbose) {
    InstallUdpDatagramBenchTraceHooks();
  }
}

void EndUdpDatagramTrace() {
  ClearUdpDatagramBenchTraceHooks();
  std::lock_guard lock{g_trace_mu};
  if (g_trace_file && g_trace_file->is_open()) {
    g_trace_file->flush();
  }
  g_trace_file.reset();
  g_trace_options = {};
}

void SetUdpDatagramTraceSide(char side) { g_trace_side = side; }

void InstallUdpDatagramBenchTraceHooks() {
  ae::SetStreamMatrixBenchTrace(BenchTraceSafeStreamSend, nullptr,
                                BenchTraceSafeStreamAck, BenchTraceSafeStreamRecv);
}

void ClearUdpDatagramBenchTraceHooks() {
  ae::SetStreamMatrixBenchTrace(nullptr, nullptr, nullptr, nullptr);
}

UdpSocketAuditSnapshot UdpSocketPayloadAuditSnapshot() {
  auto const recv_errors = g_recv_error_events.load();
  auto const emsgsize = g_recv_emsgsize_events.load();
  return UdpSocketAuditSnapshot{
      .max_tx_payload_bytes = g_max_tx.load(),
      .max_rx_payload_bytes = g_max_rx.load(),
      .tx_violations = g_tx_violations.load(),
      .rx_violations = g_rx_violations.load(),
      .recv_error_events = recv_errors,
      .recv_emsgsize_events = emsgsize,
      .recv_emsgsize_budget_lower_bound =
          g_recv_emsgsize_budget_lower_bound.load(),
      .recv_emsgsize_buffer_only = g_recv_emsgsize_buffer_only.load(),
      .recv_other_errors = recv_errors > emsgsize ? recv_errors - emsgsize : 0,
      .recv_buffer_bytes = g_recv_buffer_bytes.load(),
      .tx_observed = g_tx_observed.load(),
      .rx_observed = g_rx_observed.load(),
      .safestream_datagram_unverified = true,
  };
}

void WriteUdpAuditReport(std::ostream& out, UdpSocketAuditSnapshot const& snap) {
  out << "udp_tx_observed=" << snap.tx_observed
      << " udp_rx_observed=" << snap.rx_observed
      << " max_tx_payload=" << snap.max_tx_payload_bytes
      << " max_rx_payload=" << snap.max_rx_payload_bytes
      << " tx_violations=" << snap.tx_violations
      << " rx_violations=" << snap.rx_violations
      << " recv_emsgsize=" << snap.recv_emsgsize_events
      << " emsgsize_budget_lb=" << snap.recv_emsgsize_budget_lower_bound
      << " emsgsize_buffer_only=" << snap.recv_emsgsize_buffer_only
      << " recv_other_errors=" << snap.recv_other_errors
      << " recv_buffer_bytes=" << snap.recv_buffer_bytes
      << " safestream_per_datagram_unverified="
      << snap.safestream_datagram_unverified << '\n';
}

}  // namespace ae::stream_matrix_bench
