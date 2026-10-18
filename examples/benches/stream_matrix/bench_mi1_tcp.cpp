/*
 * Copyright 2026 Aethernet Inc.
 * 1 MiB TCP P2pStream benchmark (1024 x 1024-byte writes @ 10 ms).
 */

#include "stream_matrix/bench_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "stream_matrix/bench_mi1_frame.h"
#include "stream_matrix/bench_schedule.h"
#include "stream_matrix/bench_transport.h"

namespace ae::stream_matrix_bench {
namespace {

double BenchInstantToSec(BenchInstant t) {
  return std::chrono::duration<double>(t.time_since_epoch()).count();
}

std::string WriteStatusName(int code) {
  switch (code) {
    case 0:
      return "SUCCESS";
    case 1:
      return "FAIL";
    case 2:
      return "STOP";
    case -1:
      return "PENDING";
    default:
      return "UNKNOWN";
  }
}

struct Mi1MessageRow {
  std::uint64_t sequence{};
  BenchInstant scheduled{};
  BenchInstant write_called{};
  BenchInstant write_terminal{};
  bool have_write_terminal{false};
  int write_status{-1};
  bool schedule_overrun{false};
  BenchInstant recv{};
  bool have_recv{false};
  bool integrity_ok{false};
  bool duplicate{false};
};

double Percentile(std::vector<double> sorted, double pct) {
  if (sorted.empty()) {
    return 0.0;
  }
  auto idx = static_cast<std::size_t>(
      std::ceil(pct / 100.0 * static_cast<double>(sorted.size())) - 1.0);
  idx = std::min(idx, sorted.size() - 1);
  return sorted[idx];
}

void WriteSummaryJson(std::filesystem::path const& path,
                      std::uint64_t run_id, std::string const& sdk_sha,
                      bool schedule_overrun_any,
                      std::vector<Mi1MessageRow> const& rows,
                      BenchInstant t0, BenchInstant first_write,
                      BenchInstant last_write, BenchInstant last_unique_rx,
                      bool have_first_write, bool have_last_write,
                      std::size_t unique_rx, std::size_t dup,
                      std::size_t missing, bool integrity_all) {
  std::uint64_t scheduled = 0;
  std::uint64_t write_calls = 0;
  std::uint64_t w_ok = 0;
  std::uint64_t w_fail = 0;
  std::uint64_t w_stop = 0;
  std::uint64_t w_pending = 0;
  for (auto const& r : rows) {
    ++scheduled;
    if (r.write_called.time_since_epoch().count() != 0) {
      ++write_calls;
    }
    if (r.write_status == 0) {
      ++w_ok;
    } else if (r.write_status == 1) {
      ++w_fail;
    } else if (r.write_status == 2) {
      ++w_stop;
    } else if (r.write_status == -1 && write_calls > 0) {
      ++w_pending;
    }
  }

  std::vector<double> lat_ms;
  std::vector<double> sched_slip_ms;
  std::vector<double> send_gaps_ms;
  BenchInstant prev_write{};
  bool have_prev = false;
  for (auto const& r : rows) {
    if (r.have_recv && r.have_write_terminal) {
      lat_ms.push_back(BenchInstantDeltaMs(r.write_called, r.recv));
    }
    if (r.write_called.time_since_epoch().count() != 0) {
      sched_slip_ms.push_back(BenchInstantDeltaMs(r.scheduled, r.write_called));
      if (have_prev) {
        send_gaps_ms.push_back(BenchInstantDeltaMs(prev_write, r.write_called));
      }
      prev_write = r.write_called;
      have_prev = true;
    }
  }
  std::sort(lat_ms.begin(), lat_ms.end());
  std::sort(sched_slip_ms.begin(), sched_slip_ms.end());
  std::sort(send_gaps_ms.begin(), send_gaps_ms.end());

  double send_duration_s = 0.0;
  if (have_first_write && have_last_write) {
    send_duration_s = BenchInstantDeltaMs(first_write, last_write) / 1000.0;
  }
  double delivery_s = 0.0;
  bool const have_last_rx = last_unique_rx.time_since_epoch().count() != 0;
  if (have_first_write && have_last_rx) {
    delivery_s = BenchInstantDeltaMs(first_write, last_unique_rx) / 1000.0;
  }
  double goodput = (unique_rx == kMi1MessageCount && delivery_s > 0.0)
                       ? (1048576.0 / delivery_s)
                       : 0.0;

  bool delivery_ok = unique_rx == kMi1MessageCount && missing == 0 &&
                     integrity_all && dup == 0;

  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << std::setprecision(9) << std::fixed;
  out << "{\n";
  out << "  \"benchmark\": \"mi1_tcp_p2pstream\",\n";
  out << "  \"run_id\": " << run_id << ",\n";
  out << "  \"sdk_sha\": \"" << sdk_sha << "\",\n";
  out << "  \"frame_bytes\": " << kMi1FrameBytes << ",\n";
  out << "  \"message_count\": " << kMi1MessageCount << ",\n";
  out << "  \"period_ms\": " << kMi1PeriodMs << ",\n";
  out << "  \"scheduled\": " << scheduled << ",\n";
  out << "  \"write_called\": " << write_calls << ",\n";
  out << "  \"write_success\": " << w_ok << ",\n";
  out << "  \"write_fail\": " << w_fail << ",\n";
  out << "  \"write_stop\": " << w_stop << ",\n";
  out << "  \"write_pending\": " << w_pending << ",\n";
  out << "  \"rx_unique\": " << unique_rx << ",\n";
  out << "  \"rx_duplicate\": " << dup << ",\n";
  out << "  \"rx_missing\": " << missing << ",\n";
  out << "  \"payload_bytes_sent\": " << (write_calls * kMi1FrameBytes) << ",\n";
  out << "  \"payload_bytes_rx_unique\": " << (unique_rx * kMi1FrameBytes)
      << ",\n";
  out << "  \"integrity_ok\": " << (integrity_all ? "true" : "false") << ",\n";
  out << "  \"delivery_ok\": " << (delivery_ok ? "true" : "false") << ",\n";
  out << "  \"schedule_overrun\": " << (schedule_overrun_any ? "true" : "false")
      << ",\n";
  out << "  \"t0_epoch_s\": " << BenchInstantToSec(t0) << ",\n";
  out << "  \"send_duration_s\": ";
  if (send_duration_s > 0) {
    out << send_duration_s;
  } else {
    out << "null";
  }
  out << ",\n  \"full_delivery_s\": ";
  if (unique_rx == kMi1MessageCount && delivery_s > 0) {
    out << delivery_s;
  } else {
    out << "null";
  }
  out << ",\n  \"goodput_Bps\": ";
  if (goodput > 0) {
    out << goodput;
  } else {
    out << "null";
  }
  out << ",\n";
  auto emit_opt = [&](char const* key, std::vector<double> const& v, double pct) {
    out << "  \"" << key << "\": ";
    if (v.empty()) {
      out << "null";
    } else if (pct < 0) {
      out << v.back();
    } else {
      out << Percentile(v, pct);
    }
    out << ",\n";
  };
  emit_opt("latency_p50_ms", lat_ms, 50);
  emit_opt("latency_p95_ms", lat_ms, 95);
  emit_opt("latency_p99_ms", lat_ms, 99);
  emit_opt("latency_max_ms", lat_ms, -1);
  emit_opt("schedule_slip_p50_ms", sched_slip_ms, 50);
  emit_opt("schedule_slip_p95_ms", sched_slip_ms, 95);
  emit_opt("schedule_slip_max_ms", sched_slip_ms, -1);
  out << "  \"send_gap_p50_ms\": ";
  if (send_gaps_ms.empty()) {
    out << "null\n";
  } else {
    out << Percentile(send_gaps_ms, 50) << "\n";
  }
  out << "}\n";
}

}  // namespace

int StreamMatrixBench::RunMi1Tcp() {
  log_stream_ << "mode=mi1_tcp_p2pstream messages=" << kMi1MessageCount
              << " frame_bytes=" << kMi1FrameBytes
              << " period_ms=" << kMi1PeriodMs << '\n';

  auto setup = SetupMode(BenchStreamKind::kP2pStream, BenchTransportKind::kTcp);
  if (setup != SetupResult::kOk) {
    log_stream_ << "mi1 setup failed code=" << static_cast<int>(setup) << '\n';
    return setup == SetupResult::kNotCompiled ? 3 : 1;
  }

  std::vector<Mi1MessageRow> rows(kMi1MessageCount);
  for (std::uint64_t i = 0; i < kMi1MessageCount; ++i) {
    rows[static_cast<std::size_t>(i)].sequence = i;
  }

  std::vector<DataBuffer> expected;
  expected.reserve(kMi1MessageCount);
  for (std::uint64_t i = 0; i < kMi1MessageCount; ++i) {
    expected.push_back(MakeMi1Frame(run_id_, i));
  }

  BenchInstant const t0 = BenchNow();
  BenchInstant next_deadline = t0;
  auto const period = MsToBenchDuration(kMi1PeriodMs);
  // Allow full 10 ms spacing for all 1024 sends even after schedule shift.
  BenchInstant const send_horizon = t0 + std::chrono::seconds{600};

  std::uint64_t next_sequence = 0;
  bool sending = true;
  BenchInstant last_write{};
  BenchInstant first_write{};
  bool have_first_write = false;
  bool have_last_write = false;
  BenchInstant collect_end{};
  bool schedule_overrun_any = false;

  std::unordered_set<std::uint64_t> rx_unique;
  std::uint64_t rx_dup = 0;

  auto on_mi1_recv = [this, &rows, &rx_unique, &rx_dup](DataBuffer const& data) {
    auto hdr = ParseMi1Header(data);
    if (!hdr || hdr->run_id != run_id_) {
      return;
    }
    if (hdr->sequence >= kMi1MessageCount) {
      return;
    }
    auto& row = rows[static_cast<std::size_t>(hdr->sequence)];
    if (rx_unique.count(hdr->sequence)) {
      ++rx_dup;
      row.duplicate = true;
      return;
    }
    rx_unique.insert(hdr->sequence);
    row.have_recv = true;
    row.recv = BenchNow();
    row.integrity_ok = Mi1FrameMatchesExpected(run_id_, hdr->sequence, data);
  };

  recv_sub_.Reset();
  recv_sub_a_.Reset();
  recv_sub_ = p2p_b_->out_data_event().Subscribe(on_mi1_recv);
  recv_sub_a_ = p2p_a_->out_data_event().Subscribe(on_mi1_recv);

  while (!app_->IsExited()) {
    BenchInstant now = BenchNow();
    if (sending && next_sequence < kMi1MessageCount) {
      auto decision = EvaluateScheduleSlot(now, send_horizon, next_deadline, period);
      if (decision.missed_slots > 0) {
        schedule_overrun_any = true;
      }
      if (decision.should_send) {
        auto const seq = next_sequence++;
        auto& row = rows[static_cast<std::size_t>(seq)];
        row.scheduled = t0 + period * static_cast<int>(seq);
        row.schedule_overrun = decision.missed_slots > 0;
        auto wire = MakeMi1Frame(run_id_, seq);
        row.write_called = BenchNow();
        if (!have_first_write) {
          first_write = row.write_called;
          have_first_write = true;
        }
        last_write = row.write_called;
        have_last_write = true;
        auto& wa = send_stream_->Write(std::move(wire));
        write_status_subs_ += wa.status_event().Subscribe(
            [this, &rows, seq](WriteAction::Status status) {
              auto& row = rows[static_cast<std::size_t>(seq)];
              row.have_write_terminal = true;
              row.write_terminal = BenchNow();
              if (status == WriteAction::Status::kSuccess) {
                row.write_status = 0;
              } else if (status == WriteAction::Status::kFail) {
                row.write_status = 1;
              } else if (status == WriteAction::Status::kStop) {
                row.write_status = 2;
              }
            });
      }
      if (decision.missed_slots > 0) {
        next_deadline = now + period;
      } else {
        next_deadline = decision.next_deadline;
      }
    } else if (sending && next_sequence >= kMi1MessageCount) {
      sending = false;
      collect_end = last_write + std::chrono::seconds{60};
      log_stream_ << "mi1 send complete last_write_s="
                  << BenchInstantDeltaMs(t0, last_write) / 1000.0
                  << " collect_until_s="
                  << BenchInstantDeltaMs(t0, collect_end) / 1000.0 << '\n';
    }

    if (!sending) {
      if (rx_unique.size() >= kMi1MessageCount) {
        break;
      }
      if (collect_end.time_since_epoch().count() != 0 && now >= collect_end) {
        break;
      }
    }
    BenchDuration pump_budget = std::chrono::milliseconds{2};
    if (sending && next_sequence < kMi1MessageCount && now < next_deadline) {
      pump_budget = std::min<BenchDuration>(next_deadline - now,
                                            std::chrono::milliseconds{50});
    } else if (!sending && collect_end.time_since_epoch().count() != 0 &&
               now < collect_end) {
      pump_budget = std::min<BenchDuration>(collect_end - now,
                                            std::chrono::milliseconds{10});
    }
    PumpSdkOnce(now, pump_budget);
  }

  write_status_subs_.Reset();

  for (auto const& row : rows) {
    if (row.write_called.time_since_epoch().count() != 0) {
      first_write = row.write_called;
      have_first_write = true;
      break;
    }
  }
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
    if (it->write_called.time_since_epoch().count() != 0) {
      last_write = it->write_called;
      have_last_write = true;
      break;
    }
  }

  std::size_t missing = 0;
  bool integrity_all = true;
  BenchInstant last_unique_rx{};
  for (std::uint64_t i = 0; i < kMi1MessageCount; ++i) {
    auto const& row = rows[static_cast<std::size_t>(i)];
    if (!row.have_recv) {
      ++missing;
      integrity_all = false;
    } else {
      if (!row.integrity_ok) {
        integrity_all = false;
      }
      if (last_unique_rx.time_since_epoch().count() == 0 ||
          row.recv > last_unique_rx) {
        last_unique_rx = row.recv;
      }
    }
  }

  std::ofstream msg_csv(options_.output_dir / "messages.csv",
                        std::ios::out | std::ios::trunc);
  msg_csv << "sequence,scheduled_s,write_called_s,write_status,write_terminal_s,"
             "receive_s,latency_ms,schedule_slip_ms,schedule_overrun,integrity,"
             "duplicate\n";
  msg_csv << std::setprecision(9) << std::fixed;
  for (auto const& row : rows) {
    msg_csv << row.sequence << ',';
    msg_csv << BenchInstantToSec(row.scheduled) << ',';
    msg_csv << BenchInstantToSec(row.write_called) << ',';
    msg_csv << WriteStatusName(row.write_status) << ',';
    msg_csv << (row.have_write_terminal ? std::to_string(BenchInstantToSec(row.write_terminal))
                                        : "") << ',';
    msg_csv << (row.have_recv ? std::to_string(BenchInstantToSec(row.recv)) : "") << ',';
    if (row.have_recv && row.write_called.time_since_epoch().count() != 0) {
      msg_csv << BenchInstantDeltaMs(row.write_called, row.recv) << ',';
    } else {
      msg_csv << ',';
    }
    if (row.write_called.time_since_epoch().count() != 0) {
      msg_csv << BenchInstantDeltaMs(row.scheduled, row.write_called) << ',';
    } else {
      msg_csv << ',';
    }
    msg_csv << (row.schedule_overrun ? "1" : "0") << ',';
    if (!row.have_recv) {
      msg_csv << "MISSING,";
    } else {
      msg_csv << (row.integrity_ok ? "OK" : "BAD") << ',';
    }
    msg_csv << (row.duplicate ? "1" : "0") << '\n';
  }

  std::string sdk = options_.sdk_sha;
  if (options_.sdk_tree_dirty) {
    sdk += "-dirty";
  }
  WriteSummaryJson(options_.output_dir / "summary.json", run_id_, sdk,
                   schedule_overrun_any, rows, t0, first_write, last_write,
                   last_unique_rx, have_first_write, have_last_write,
                   rx_unique.size(), rx_dup, missing, integrity_all);

  log_stream_ << "mi1 rx_unique=" << rx_unique.size() << " dup=" << rx_dup
              << " missing=" << missing << " integrity_all=" << integrity_all
              << " schedule_overrun=" << schedule_overrun_any << '\n';

  TeardownStreams();
  app_->Exit(0);

  if (missing == 0 && rx_unique.size() == kMi1MessageCount && integrity_all &&
      rx_dup == 0) {
    return 0;
  }
  return 2;
}

}  // namespace ae::stream_matrix_bench
