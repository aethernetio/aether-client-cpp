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

#include "stream_matrix/bench_stats.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace ae::stream_matrix_bench {

void PointStats::BeginPoint(BenchInstant measure_begin, BenchInstant measure_end,
                            BenchInstant collect_end) {
  measure_begin_ = measure_begin;
  measure_end_ = measure_end;
  collect_end_ = collect_end;
  counters_ = {};
  measure_sends_.clear();
  received_unique_measure_.clear();
  received_unique_all_.clear();
  have_last_sequence_ = false;
  send_receive_latency_ms_.clear();
  schedule_latency_ms_.clear();
  schedule_receive_latency_ms_.clear();
  goodput_payload_bytes_ = 0;
}

void PointStats::NoteScheduledMeasure() { ++counters_.scheduled_measure; }

void PointStats::NoteMissedSchedule(std::uint64_t count) {
  counters_.missed_schedule += count;
}

void PointStats::NoteWriteCalledMeasure(std::uint64_t sequence,
                                        BenchInstant send_time,
                                        BenchInstant scheduled_time) {
  ++counters_.write_calls_measure;
  measure_sends_[sequence] = SendRecord{
      .sequence = sequence,
      .send_time = send_time,
      .scheduled_time = scheduled_time,
      .in_measure = true,
  };
  auto sched_ms = BenchInstantDeltaMs(scheduled_time, send_time);
  schedule_latency_ms_.push_back(sched_ms);
}

void PointStats::NoteWriteOkMeasure() { ++counters_.write_ok_measure; }

void PointStats::NoteWriteFailedMeasure() {
  ++counters_.write_failed_measure;
}

void PointStats::NoteWriteStoppedMeasure() {
  ++counters_.write_stopped_measure;
}

void PointStats::NoteWriteImmediateFailMeasure() {
  ++counters_.write_immediate_fail_measure;
}

void PointStats::NoteSkippedBackpressure() {
  ++counters_.skipped_backpressure;
}

void PointStats::NoteFirstReceive(std::uint64_t sequence, BenchInstant recv_time,
                                  std::size_t payload_bytes,
                                  bool in_measure_window) {
  auto send_it = measure_sends_.find(sequence);
  if (send_it == measure_sends_.end()) {
    return;
  }

  if (!received_unique_all_.insert(sequence).second) {
    ++counters_.rx_duplicate;
    return;
  }

  ++counters_.rx_unique_by_deadline;

  if (in_measure_window) {
    ++counters_.rx_unique_measure;
    if (have_last_sequence_ && sequence < last_received_sequence_) {
      ++counters_.reordered;
    }
    last_received_sequence_ = sequence;
    have_last_sequence_ = true;
    goodput_payload_bytes_ += payload_bytes;
  } else {
    ++counters_.late;
  }

  auto ms = BenchInstantDeltaMs(send_it->second.send_time, recv_time);
  send_receive_latency_ms_.push_back(ms);
  ++counters_.latency_samples;

  auto sched_ms =
      BenchInstantDeltaMs(send_it->second.scheduled_time, recv_time);
  schedule_receive_latency_ms_.push_back(sched_ms);
}

void PointStats::NoteReceivedMeasure(std::uint64_t sequence,
                                     BenchInstant recv_time,
                                     std::size_t payload_bytes) {
  if (measure_sends_.count(sequence) == 0) {
    return;
  }
  if (recv_time < measure_begin_) {
    return;
  }
  if (recv_time > collect_end_) {
    return;
  }
  if (recv_time <= measure_end_) {
    NoteFirstReceive(sequence, recv_time, payload_bytes, true);
    return;
  }
  NoteReceivedLate(sequence, recv_time, payload_bytes);
}

void PointStats::NoteReceivedLate(std::uint64_t sequence, BenchInstant recv_time,
                                  std::size_t payload_bytes) {
  if (measure_sends_.count(sequence) == 0) {
    return;
  }
  if (recv_time <= measure_end_ || recv_time > collect_end_) {
    return;
  }
  NoteFirstReceive(sequence, recv_time, payload_bytes, false);
}

void PointStats::FinalizeAfterCollectEnd() {
  for (auto const& [seq, rec] : measure_sends_) {
    (void)rec;
    if (received_unique_all_.count(seq) == 0) {
      ++counters_.missing_after_deadline;
    }
  }
}

double PointStats::PercentileMs(std::vector<double> const& sorted, double pct) {
  if (sorted.empty()) {
    return 0.0;
  }
  auto idx = static_cast<std::size_t>(
      std::ceil(pct / 100.0 * static_cast<double>(sorted.size())) - 1.0);
  idx = std::min(idx, sorted.size() - 1);
  return sorted[idx];
}

LatencySummary PointStats::SendReceiveLatency() const {
  if (send_receive_latency_ms_.empty()) {
    return {};
  }
  auto sorted = send_receive_latency_ms_;
  std::sort(sorted.begin(), sorted.end());
  return LatencySummary{
      .p50_ms = PercentileMs(sorted, 50.0),
      .p95_ms = PercentileMs(sorted, 95.0),
      .p99_ms = PercentileMs(sorted, 99.0),
      .max_ms = sorted.back(),
  };
}

ScheduleLatencySummary PointStats::ScheduleLatency() const {
  if (schedule_latency_ms_.empty()) {
    return {};
  }
  auto sorted = schedule_latency_ms_;
  std::sort(sorted.begin(), sorted.end());
  return ScheduleLatencySummary{
      .p50_ms = PercentileMs(sorted, 50.0),
      .p95_ms = PercentileMs(sorted, 95.0),
      .max_ms = sorted.back(),
  };
}

ScheduleReceiveLatencySummary PointStats::ScheduleReceiveLatency() const {
  if (schedule_receive_latency_ms_.empty()) {
    return {};
  }
  auto sorted = schedule_receive_latency_ms_;
  std::sort(sorted.begin(), sorted.end());
  return ScheduleReceiveLatencySummary{
      .p50_ms = PercentileMs(sorted, 50.0),
      .p95_ms = PercentileMs(sorted, 95.0),
      .max_ms = sorted.back(),
  };
}

double PointStats::GoodputBytesPerSec() const {
  auto window_s = std::chrono::duration_cast<std::chrono::duration<double>>(
      measure_end_ - measure_begin_);
  if (window_s.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(goodput_payload_bytes_) / window_s.count();
}

double PointStats::SendRatePerSec() const {
  auto window_s = std::chrono::duration_cast<std::chrono::duration<double>>(
      measure_end_ - measure_begin_);
  if (window_s.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(counters_.write_calls_measure) / window_s.count();
}

double PointStats::ReceiveRatePerSec() const {
  auto window_s = std::chrono::duration_cast<std::chrono::duration<double>>(
      measure_end_ - measure_begin_);
  if (window_s.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(counters_.rx_unique_measure) / window_s.count();
}

static void WriteOptionalMs(std::ostream& out, std::optional<double> v) {
  if (v) {
    out << *v;
  } else {
    out << "N/A";
  }
}

void WriteCsvHeader(std::ostream& out) {
  out << "run_id,point_id,transport,stream,payload_bytes,serialized_bytes,"
         "max_udp_payload_bytes,period_ms,scheduled,missed_schedule,write_calls,"
         "write_ok,write_failed,write_stopped,write_immediate_fail,skipped,"
         "rx_unique,rx_unique_by_deadline,rx_duplicate,reordered,"
         "missing_after_deadline,delivery_loss_pct,late,send_rate,receive_rate,"
         "goodput_bytes_s,latency_p50_ms,latency_p95_ms,latency_p99_ms,"
         "latency_max_ms,sched_p50_ms,sched_p95_ms,sched_max_ms,sched_rx_p50_ms,"
         "sched_rx_p95_ms,sched_rx_max_ms,max_udp_tx_observed,max_udp_rx_observed,"
         "udp_tx_violations,udp_rx_violations,udp_socket_observed,SDK_SHA,"
         "server_version,outcome\n";
}

void WriteCsvRow(std::ostream& out, CsvRow const& row) {
  out << row.run_id << ',' << row.point_id << ',' << row.transport << ','
      << row.stream << ',' << row.payload_bytes << ',' << row.serialized_bytes
      << ',' << row.max_udp_payload_bytes << ',' << row.period_ms << ','
      << row.counters.scheduled_measure << ','
      << row.counters.missed_schedule << ','
      << row.counters.write_calls_measure << ','
      << row.counters.write_ok_measure << ','
      << row.counters.write_failed_measure << ','
      << row.counters.write_stopped_measure << ','
      << row.counters.write_immediate_fail_measure << ','
      << row.counters.skipped_backpressure << ','
      << row.counters.rx_unique_measure << ','
      << row.counters.rx_unique_by_deadline << ','
      << row.counters.rx_duplicate << ',' << row.counters.reordered << ','
      << row.counters.missing_after_deadline << ','
      << std::fixed << std::setprecision(3) << row.delivery_loss_pct << ','
      << row.counters.late << ',' << row.send_rate << ',' << row.receive_rate
      << ',' << row.goodput_bytes_s << ',';
  WriteOptionalMs(out, row.latency.p50_ms);
  out << ',';
  WriteOptionalMs(out, row.latency.p95_ms);
  out << ',';
  WriteOptionalMs(out, row.latency.p99_ms);
  out << ',';
  WriteOptionalMs(out, row.latency.max_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_latency.p50_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_latency.p95_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_latency.max_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_receive_latency.p50_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_receive_latency.p95_ms);
  out << ',';
  WriteOptionalMs(out, row.schedule_receive_latency.max_ms);
  out << ',' << row.max_udp_tx_observed << ',' << row.max_udp_rx_observed << ','
      << row.udp_tx_violations << ',' << row.udp_rx_violations << ','
      << (row.udp_socket_observed ? 1 : 0) << ',' << row.sdk_sha << ','
      << row.server_version << ',' << row.outcome << '\n';
}

}  // namespace ae::stream_matrix_bench
