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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "aether/clock.h"

namespace ae::stream_matrix_bench {

struct SendRecord {
  std::uint64_t sequence{};
  TimePoint send_time{};
  TimePoint scheduled_time{};
  bool counted_measure{};
};

struct PointCounters {
  std::uint64_t scheduled{};
  std::uint64_t write_calls{};
  std::uint64_t write_ok{};
  std::uint64_t write_failed{};
  std::uint64_t write_stopped{};
  std::uint64_t skipped_backpressure{};
  std::uint64_t rx_unique{};
  std::uint64_t rx_duplicate{};
  std::uint64_t reordered{};
  std::uint64_t late{};
  std::uint64_t missing_after_deadline{};
};

struct LatencySummary {
  std::optional<double> p50_ms;
  std::optional<double> p95_ms;
  std::optional<double> p99_ms;
  std::optional<double> max_ms;
};

class PointStats {
 public:
  void ResetMeasureWindow(TimePoint measure_begin, TimePoint measure_end);

  void NoteScheduled();
  void NoteWriteCalled(std::uint64_t sequence, TimePoint send_time,
                       TimePoint scheduled_time, bool in_measure);
  void NoteWriteOk();
  void NoteWriteFailed();
  void NoteWriteStopped();
  void NoteSkippedBackpressure();

  void NoteReceived(std::uint64_t sequence, TimePoint recv_time,
                    std::size_t payload_bytes);

  void FinalizeAfterDeadline(TimePoint deadline);

  PointCounters const& counters() const { return counters_; }
  LatencySummary Latency() const;
  double GoodputBytesPerSec() const;
  double SendRate() const;
  double ReceiveRate() const;

 private:
  static double PercentileMs(std::vector<double> const& sorted, double pct);

  TimePoint measure_begin_{};
  TimePoint measure_end_{};
  PointCounters counters_{};
  std::unordered_map<std::uint64_t, SendRecord> sends_;
  std::unordered_set<std::uint64_t> received_unique_;
  std::uint64_t last_received_sequence_{0};
  bool have_last_sequence_{false};
  std::vector<double> latency_ms_measure_;
  std::size_t unique_payload_bytes_measure_{};
};

struct CsvRow {
  std::string transport;
  std::string stream;
  std::size_t payload_bytes{};
  std::size_t serialized_bytes{};
  std::size_t max_udp_payload_bytes{};
  double period_ms{};
  PointCounters counters;
  LatencySummary latency;
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

#endif  // EXAMPLES_BENCHES_STREAM_MATRIX_BENCH_STATS_H_
