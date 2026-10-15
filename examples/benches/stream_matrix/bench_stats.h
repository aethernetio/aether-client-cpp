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

#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STATS_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STATS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stream_matrix/bench_clock.h"

namespace ae::stream_matrix_bench {

struct SendRecord {
  std::uint64_t sequence{};
  BenchInstant send_time{};
  BenchInstant scheduled_time{};
  bool in_measure{};
};

struct PointCounters {
  std::uint64_t scheduled_measure{};
  std::uint64_t missed_schedule{};
  std::uint64_t write_calls_measure{};
  std::uint64_t write_ok_measure{};
  std::uint64_t write_failed_measure{};
  std::uint64_t write_stopped_measure{};
  std::uint64_t write_immediate_fail_measure{};
  std::uint64_t skipped_backpressure{};
  std::uint64_t rx_unique_measure{};
  std::uint64_t rx_unique_by_deadline{};
  std::uint64_t rx_duplicate{};
  std::uint64_t reordered{};
  std::uint64_t late{};
  std::uint64_t missing_after_deadline{};
  std::uint64_t latency_samples{};
};

struct LatencySummary {
  std::optional<double> p50_ms;
  std::optional<double> p95_ms;
  std::optional<double> p99_ms;
  std::optional<double> max_ms;
};

struct ScheduleLatencySummary {
  std::optional<double> p50_ms;
  std::optional<double> p95_ms;
  std::optional<double> max_ms;
};

struct ScheduleReceiveLatencySummary {
  std::optional<double> p50_ms;
  std::optional<double> p95_ms;
  std::optional<double> max_ms;
};

class PointStats {
 public:
  void BeginPoint(BenchInstant measure_begin, BenchInstant measure_end,
                  BenchInstant collect_end);

  void NoteScheduledMeasure();
  void NoteMissedSchedule(std::uint64_t count = 1);
  void NoteWriteCalledMeasure(std::uint64_t sequence, BenchInstant send_time,
                              BenchInstant scheduled_time);
  void NoteWriteOkMeasure();
  void NoteWriteFailedMeasure();
  void NoteWriteStoppedMeasure();
  void NoteWriteImmediateFailMeasure();
  void NoteSkippedBackpressure();

  void NoteReceivedMeasure(std::uint64_t sequence, BenchInstant recv_time,
                           std::size_t payload_bytes);
  void NoteReceivedLate(std::uint64_t sequence, BenchInstant recv_time,
                        std::size_t payload_bytes);

  void FinalizeAfterCollectEnd();

  PointCounters const& counters() const { return counters_; }
  LatencySummary SendReceiveLatency() const;
  ScheduleLatencySummary ScheduleLatency() const;
  ScheduleReceiveLatencySummary ScheduleReceiveLatency() const;
  double GoodputBytesPerSec() const;
  double SendRatePerSec() const;
  double ReceiveRatePerSec() const;

 private:
  static double PercentileMs(std::vector<double> const& sorted, double pct);

  BenchInstant measure_begin_{};
  BenchInstant measure_end_{};
  BenchInstant collect_end_{};
  PointCounters counters_{};
  std::unordered_map<std::uint64_t, SendRecord> measure_sends_;
  std::unordered_set<std::uint64_t> received_unique_measure_;
  std::unordered_set<std::uint64_t> received_unique_all_;
  std::uint64_t last_received_sequence_{0};
  bool have_last_sequence_{false};
  std::vector<double> send_receive_latency_ms_;
  std::vector<double> schedule_latency_ms_;
  std::vector<double> schedule_receive_latency_ms_;
  std::size_t goodput_payload_bytes_{};

  void NoteFirstReceive(std::uint64_t sequence, BenchInstant recv_time,
                        std::size_t payload_bytes, bool in_measure_window);
};

struct CsvRow {
  std::string run_id;
  std::string point_id;
  std::string transport;
  std::string stream;
  std::size_t payload_bytes{};
  std::size_t serialized_bytes{};
  std::size_t max_udp_payload_bytes{};
  double period_ms{};
  PointCounters counters;
  LatencySummary latency;
  ScheduleLatencySummary schedule_latency;
  ScheduleReceiveLatencySummary schedule_receive_latency;
  double send_rate{};
  double receive_rate{};
  double goodput_bytes_s{};
  double delivery_loss_pct{};
  std::string sdk_sha;
  std::string server_version;
  std::string outcome;
};

void WriteCsvHeader(std::ostream& out);
void WriteCsvRow(std::ostream& out, CsvRow const& row);

}  // namespace ae::stream_matrix_bench

#endif
