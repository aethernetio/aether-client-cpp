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

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "aether/stream_api/istream.h"

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_limits.h"
#include "stream_matrix/bench_stats.h"
#include "stream_matrix/bench_streaming_receiver.h"
#include "stream_matrix/bench_throughput_frame.h"
#include "stream_matrix/bench_udp_budget.h"
#include "stream_matrix/stream_matrix_win_udp_recv_test.h"

using namespace ae::stream_matrix_bench;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static BenchInstant T(int ms) {
  return BenchInstant{} + std::chrono::milliseconds{ms};
}

static void test_late_duplicate_and_missing() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteWriteCalledMeasure(1, T(100), T(0));
  stats.NoteWriteCalledMeasure(2, T(200), T(100));
  stats.NoteWriteCalledMeasure(3, T(300), T(200));

  stats.NoteReceivedMeasure(1, T(150), 100);
  stats.NoteReceivedMeasure(2, T(1500), 100);
  stats.NoteReceivedMeasure(2, T(1600), 100);
  stats.FinalizeAfterCollectEnd();

  auto c = stats.counters();
  Expect(c.rx_unique_by_deadline == 2, "unique by deadline");
  Expect(c.rx_unique_measure == 1, "in-window unique");
  Expect(c.late == 1, "late unique");
  Expect(c.rx_duplicate == 1, "duplicate late");
  Expect(c.missing_after_deadline == 1, "missing seq 3");
  Expect(c.latency_samples == 2, "latency samples");
  Expect(stats.GoodputBytesPerSec() > 0.0, "goodput in-window only");
}

static void test_unknown_sequence_ignored() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteReceivedMeasure(99, T(500), 100);
  Expect(stats.counters().rx_duplicate == 0 &&
             stats.counters().rx_unique_by_deadline == 0,
         "unknown sequence ignored");
}

static void test_immediate_fail_counter_only() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteWriteImmediateFailMeasure();
  Expect(stats.counters().write_calls_measure == 0, "no write call on fail");
  Expect(stats.counters().write_immediate_fail_measure == 1, "immediate fail");
}

static void test_udp_outcome_safestream_vs_p2p() {
  ae::StreamInfo info{};
  info.max_element_size = 1200;
  info.link_state = ae::LinkState::kLinked;
  UdpSocketAuditSnapshot clean{};
  clean.socket_observed = true;
  std::size_t const large_serialized = 5000;

  auto safe_out = ClassifyUdpMeasureOutcome(
      BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp, {}, large_serialized,
      info, clean);
  Expect(safe_out.empty(), "SafeStream/UDP large message no envelope estimate fail");

  auto p2p_out = ClassifyUdpMeasureOutcome(
      BenchStreamKind::kP2pStream, BenchTransportKind::kUdp, {}, large_serialized,
      info, clean);
  Expect(p2p_out == "UDP_ENVELOPE_ESTIMATE_EXCEEDED",
         "P2pStream/UDP large message envelope estimate fail");

  UdpSocketAuditSnapshot bad{};
  bad.tx_violations = 1;
  bad.socket_observed = true;
  auto safe_viol = ClassifyUdpMeasureOutcome(
      BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp, {}, large_serialized,
      info, bad);
  Expect(safe_viol == "UDP_SOCKET_BUDGET_VIOLATION",
         "SafeStream/UDP datagram audit violation");
}

static void test_udp_budget_boundary() {
  ae::StreamInfo info{};
  info.max_element_size = 1200;
  info.link_state = ae::LinkState::kLinked;
  auto est = EstimateUdpBenchEnvelopeBudget(info);
  Expect(est.max_bench_envelope_bytes > 100, "budget allows payload");
  Expect(BenchEnvelopeWithinUdpSocketBudget(est.max_bench_envelope_bytes, info),
         "max serialized envelope within budget");
  Expect(!BenchEnvelopeWithinUdpSocketBudget(est.max_bench_envelope_bytes + 1,
                                             info),
         "next byte exceeds budget");
}

static void test_period_generation_respects_max_rate() {
  auto periods = PeriodsMsFast(4.0);
  Expect(!periods.empty(), "periods non-empty");
  for (double p : periods) {
    Expect((1000.0 / p) <= 4.0 + 1e-6, "period respects max rate");
  }
}

static void test_missed_schedule_counter() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteMissedSchedule();
  stats.NoteMissedSchedule();
  Expect(stats.counters().missed_schedule == 2, "missed schedule");
}

static void test_thpt_wire_discriminator() {
  auto blob = BuildThroughputBlob();
  auto thpt = MakeThptChunk(blob, 1, 0, 0, 1024);
  auto env = SerializeEnvelope(BenchEnvelope{
      .kind = static_cast<std::uint8_t>(EnvelopeKind::kService),
      .run_id = 1,
      .point_id = 0,
      .sequence = 1,
      .payload = MakePayloadPattern(8),
  });
  Expect(IsThptWireData(thpt), "thpt buffer recognized");
  Expect(!IsThptWireData(env), "service envelope not thpt");
}

static void test_streaming_receiver_full_mib() {
  auto blob = BuildThroughputBlob();
  StreamingReceiverState rx(blob, 42);
  std::size_t const chunk = 4096;
  auto const chunks = (kThroughputTotalBytes + chunk - 1) / chunk;
  for (std::uint64_t i = 0; i < chunks; ++i) {
    auto off = i * chunk;
    auto len = std::min(chunk, kThroughputTotalBytes - off);
    rx.Feed(MakeThptChunk(blob, 42, i, off, len), T(static_cast<int>(i)));
  }
  Expect(rx.Complete(), "full mib reassembled");
  Expect(rx.bytes_unique() == kThroughputTotalBytes, "byte count");
}

int main() {
  test_late_duplicate_and_missing();
  test_unknown_sequence_ignored();
  test_immediate_fail_counter_only();
  test_udp_outcome_safestream_vs_p2p();
  test_udp_budget_boundary();
  test_period_generation_respects_max_rate();
  test_missed_schedule_counter();
  test_thpt_wire_discriminator();
  test_streaming_receiver_full_mib();
#if defined(_WIN32)
  RunStreamMatrixWinUdpRecvTests();
  g_failures += stream_matrix_win_udp_recv_test_failures;
#endif
  if (g_failures != 0) {
    std::cerr << "stream_matrix_logic_test: " << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_logic_test: OK\n";
  return 0;
}
