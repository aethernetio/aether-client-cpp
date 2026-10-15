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

#include <cmath>
#include <iomanip>
#include <iostream>

namespace ae::stream_matrix_bench {

void PointStats::ResetMeasureWindow(TimePoint measure_begin,
                                    TimePoint measure_end) {
  measure_begin_ = measure_begin;
  measure_end_ = measure_end;
  counters_ = {};
  sends_.clear();
  received_unique_.clear();
  have_last_sequence_ = false;
  latency_ms_measure_.clear();
  unique_payload_bytes_measure_ = 0;
}

void PointStats::NoteScheduled() { ++counters_.scheduled; }

void PointStats::NoteWriteCalled(std::uint64_t sequence, TimePoint send_time,
                                 TimePoint scheduled_time, bool in_measure) {
  ++counters_.write_calls;
  sends_[sequence] = SendRecord{
      .sequence = sequence,
      .send_time = send_time,
      .scheduled_time = scheduled_time,
      .counted_measure = in_measure,
  };
}

void PointStats::NoteWriteOk() { ++counters_.write_ok; }

void PointStats::NoteWriteFailed() { ++counters_.write_failed; }

void PointStats::NoteWriteStopped() { ++counters_.write_stopped; }

void PointStats::NoteSkippedBackpressure() {
  ++counters_.skipped_backpressure;
}

void PointStats::NoteReceived(std::uint64_t sequence, TimePoint recv_time,
                              std::size_t payload_bytes) {
  if (!received_unique_.insert(sequence).second) {
    ++counters_.rx_duplicate;
    return;
  }
  ++counters_.rx_unique;

  if (have_last_sequence_ && sequence < last_received_sequence_) {
    ++counters_.reordered;
  }
  last_received_sequence_ = sequence;
  have_last_sequence_ = true;

  auto send_it = sends_.find(sequence);
  if (send_it != sends_.end()) {
    auto ms = std::chrono::duration<double, std::milli>(recv_time -
                                                        send_it->second.send_time)
                  .count();
    if (send_it->second.counted_measure && recv_time >= measure_begin_ &&
        recv_time <= measure_end_) {
      latency_ms_measure_.push_back(ms);
      unique_payload_bytes_measure_ += payload_bytes;
    } else if (recv_time > measure_end_) {
      ++counters_.late;
    }
  }
}

void PointStats::FinalizeAfterDeadline(TimePoint /*deadline*/) {
  for (auto const& [seq, rec] : sends_) {
    if (received_unique_.count(seq) == 0) {
      ++counters_.missing_after_deadline;
    }
    (void)rec;
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

LatencySummary PointStats::Latency() const {
  if (latency_ms_measure_.empty()) {
    return {};
  }
  auto sorted = latency_ms_measure_;
  std::sort(sorted.begin(), sorted.end());
  return LatencySummary{
      .p50_ms = PercentileMs(sorted, 50.0),
      .p95_ms = PercentileMs(sorted, 95.0),
      .p99_ms = PercentileMs(sorted, 99.0),
      .max_ms = sorted.back(),
  };
}

double PointStats::GoodputBytesPerSec() const {
  auto window = std::chrono::duration<double>(measure_end_ - measure_begin_);
  if (window.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(unique_payload_bytes_measure_) / window.count();
}

double PointStats::SendRate() const {
  auto window = std::chrono::duration<double>(measure_end_ - measure_begin_);
  if (window.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(counters_.write_calls) / window.count();
}

double PointStats::ReceiveRate() const {
  auto window = std::chrono::duration<double>(measure_end_ - measure_begin_);
  if (window.count() <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(counters_.rx_unique) / window.count();
}

static void WriteOptionalMs(std::ostream& out, std::optional<double> v) {
  if (v) {
    out << *v;
  } else {
    out << "N/A";
  }
}

void WriteCsvHeader(std::ostream& out) {
  out << "transport,stream,payload_bytes,serialized_bytes,max_udp_payload_"
         "bytes,period_ms,scheduled,write_calls,write_ok,write_failed,"
         "skipped,rx_unique,rx_duplicate,reordered,missing_after_deadline,"
         "delivery_loss_pct,late,send_rate,receive_rate,goodput_bytes_s,"
         "latency_p50_ms,latency_p95_ms,latency_p99_ms,latency_max_ms,"
         "SDK_SHA,server_version,outcome\n";
}

void WriteCsvRow(std::ostream& out, CsvRow const& row) {
  out << row.transport << ',' << row.stream << ',' << row.payload_bytes << ','
      << row.serialized_bytes << ',' << row.max_udp_payload_bytes << ','
      << row.period_ms << ',' << row.counters.scheduled << ','
      << row.counters.write_calls << ',' << row.counters.write_ok << ','
      << row.counters.write_failed << ',' << row.counters.skipped_backpressure
      << ',' << row.counters.rx_unique << ',' << row.counters.rx_duplicate
      << ',' << row.counters.reordered << ','
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
  out << ',' << row.sdk_sha << ',' << row.server_version << ',' << row.outcome
      << '\n';
}

}  // namespace ae::stream_matrix_bench
