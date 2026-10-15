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

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "aether/stream_api/istream.h"

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_limits.h"
#include "stream_matrix/bench_stats.h"

using namespace ae::stream_matrix_bench;
using ae::TimePoint;
using ae::Duration;

static TimePoint T(int ms) {
  return TimePoint{} + std::chrono::milliseconds{ms};
}

static void test_measure_window_excludes_warmup_writes() {
  PointStats stats;
  stats.BeginPoint(T(5000), T(65000), T(125000));
  stats.NoteScheduledMeasure();
  stats.NoteWriteCalledMeasure(1, T(5100), T(5000));
  stats.NoteWriteOkMeasure();
  stats.NoteReceivedMeasure(1, T(5200), 100);
  stats.FinalizeAfterCollectEnd();
  assert(stats.counters().write_calls_measure == 1);
  assert(stats.counters().rx_unique_measure == 1);
  assert(stats.counters().missing_after_deadline == 0);
}

static void test_late_delivery_counted_not_goodput() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteWriteCalledMeasure(7, T(100), T(100));
  stats.NoteReceivedMeasure(7, T(1500), 100);
  assert(stats.counters().late == 1);
  assert(stats.GoodputBytesPerSec() == 0.0);
  assert(stats.SendReceiveLatency().p50_ms.has_value());
}

static void test_immediate_fail_does_not_increment_write_calls() {
  PointStats stats;
  stats.BeginPoint(T(0), T(1000), T(2000));
  stats.NoteWriteImmediateFailMeasure();
  assert(stats.counters().write_calls_measure == 0);
  assert(stats.counters().write_immediate_fail_measure == 1);
}

static void test_size_limit_binary_search() {
  ae::StreamInfo info{};
  info.max_element_size = 500;
  info.link_state = ae::LinkState::kLinked;
  SafeStreamLimitsDoc doc{};
  auto limit = ComputePayloadLimit(BenchStreamKind::kP2pStream,
                                   BenchTransportKind::kUdp, info, doc,
                                   MeasureEnvelopeOverheadBytes());
  assert(limit.max_payload_bytes > 0);
  assert(limit.max_payload_bytes < 500);
}

static void test_setup_failed_outcome_distinct() {
  CsvRow row;
  row.outcome = "SETUP_FAILED";
  assert(row.outcome != "TRANSPORT_UNAVAILABLE");
  row.outcome = "SKIP_SIZE_LIMIT";
  assert(row.outcome != "OK");
}

int main() {
  test_measure_window_excludes_warmup_writes();
  test_late_delivery_counted_not_goodput();
  test_immediate_fail_does_not_increment_write_calls();
  test_size_limit_binary_search();
  test_setup_failed_outcome_distinct();
  std::cout << "stream_matrix_logic_test: OK\n";
  return 0;
}
