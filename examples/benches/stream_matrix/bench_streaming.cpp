/*
 * Copyright 2026 Aethernet Inc.
 * Max-speed 1 MiB streaming (TCP/P2pStream, UDP/P2pSafeStream).
 */

#include "stream_matrix/bench_runner.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "stream_matrix/bench_streaming_common.h"
#include "stream_matrix/bench_streaming_load.h"
#include "stream_matrix/bench_throughput_frame.h"
#include "stream_matrix/bench_transport.h"
#include "stream_matrix/bench_udp_budget.h"

namespace ae::stream_matrix_bench {
namespace {

inline constexpr std::chrono::seconds kPointDeadline{120};
inline constexpr std::size_t kUdpBudgetPayloadBytes = 1200;

std::chrono::seconds StreamingPointDeadline(BenchCliOptions const& options) {
  if (options.streaming_point_deadline_s && *options.streaming_point_deadline_s > 0) {
    return std::chrono::seconds{*options.streaming_point_deadline_s};
  }
  return kPointDeadline;
}

char const* LoadModeName(LoadMode m) {
  return m == LoadMode::kBurst ? "burst" : "backpressure";
}

LoadMode ParseLoadModeName(std::string const& s) {
  return s == "backpressure" ? LoadMode::kBackpressure : LoadMode::kBurst;
}

using PlannedPoint = StreamingPlannedPoint;

std::string PointArtifactTag(PlannedPoint const& p, int repeat) {
  return std::to_string(p.chunk) + '_' + std::to_string(p.outstanding) + '_' +
         LoadModeName(p.mode) + "_r" + std::to_string(repeat);
}

std::string SdkSha(BenchCliOptions const& opt) {
  std::string sdk = opt.sdk_sha;
  if (opt.sdk_tree_dirty) {
    sdk += "-dirty";
  }
  return sdk;
}

using PointKey = std::tuple<std::size_t, std::size_t, std::string>;

PointKey PointKeyOf(PlannedPoint const& p) {
  return {p.chunk, p.outstanding, LoadModeName(p.mode)};
}

std::set<PointKey> LoadCompletedPointKeys(std::filesystem::path const& csv_path) {
  std::set<PointKey> done;
  std::ifstream in(csv_path);
  if (!in) {
    return done;
  }
  std::string line;
  if (!std::getline(in, line)) {
    return done;
  }
  while (std::getline(in, line)) {
    if (line.empty() || line.rfind("SETUP_FAILED", 0) == 0) {
      continue;
    }
    std::stringstream ss(line);
    std::string cell;
    std::vector<std::string> cols;
    while (std::getline(ss, cell, ',')) {
      cols.push_back(cell);
    }
    if (cols.size() < 5) {
      continue;
    }
    if (line.find("SETUP_FAILED") != std::string::npos) {
      continue;
    }
    try {
      auto chunk = static_cast<std::size_t>(std::stoull(cols[3]));
      auto outstanding = static_cast<std::size_t>(std::stoull(cols[4]));
      auto mode = cols[2];
      done.insert({chunk, outstanding, mode});
    } catch (...) {
    }
  }
  return done;
}

void WriteStreamingHeader(std::ofstream& csv) {
  csv << "transport,stream,mode,block_bytes,outstanding,repeat,chunks_total,"
         "payload_bytes_total,stream_write_bytes_total,write_attempts,write_ok,"
         "write_fail,write_stop,write_pending,write_unknown,generator_limit_events,"
         "sdk_capacity_events,events_start,events_peak,events_end,handlers_start,"
         "handlers_peak,handlers_end,write_status_trusted,peak_outstanding_writes,"
         "peak_outstanding_bytes,"
         "dup_chunks,rx_unique_bytes,submit_duration_s,first_write_to_rx_s,"
         "first_write_to_last_rx_s,all_writes_terminal_s,goodput_mib_s,"
         "write_terminal_p50_ms,write_terminal_p95_ms,write_to_rx_p50_ms,"
         "write_to_rx_p95_ms,write_to_rx_max_ms,udp_max_tx,udp_max_rx,"
         "udp_tx_violations,udp_rx_violations,delivery_ok,budget_ok,"
         "valid_goodput_mib_s,udp_budget_status,integrity_ok,outcome,sdk_sha\n";
}

void WriteStreamingRow(std::ofstream& csv, StreamingRunResult const& r,
                       std::string const& sdk) {
  auto const payload_total = r.payload_total_bytes;
  std::uint64_t const stream_write_bytes =
      r.chunks_total > 0
          ? r.chunks_total * (sizeof(ThptWireHeader) + r.chunk_bytes)
          : 0;
  csv << TransportKindName(r.transport) << ',' << StreamKindName(r.stream) << ','
      << LoadModeName(r.mode) << ',' << r.chunk_bytes << ','
      << r.outstanding_limit << ',' << r.repeat << ',' << r.chunks_total << ','
      << payload_total << ',' << stream_write_bytes << ',' << r.write_attempts
      << ',' << r.write_ok << ',' << r.write_fail << ',' << r.write_stop << ','
      << r.write_pending << ',' << r.write_unknown << ','
      << r.generator_limit_events << ',' << r.sdk_capacity_events << ','
      << r.events_registered_start << ',' << r.events_registered_peak << ','
      << r.events_registered_end << ',' << r.handlers_active_start << ','
      << r.handlers_active_peak << ',' << r.handlers_active_end << ','
      << (r.write_status_trusted ? 1 : 0) << ',' << r.peak_outstanding_writes
      << ','
      << r.peak_outstanding_bytes << ',' << r.dup_chunks << ','
      << r.rx_unique_bytes << ',' << r.submit_duration_s << ',';
  if (r.first_rx_s > 0) {
    csv << r.first_rx_s;
  }
  csv << ',';
  if (r.last_rx_s > 0) {
    csv << r.last_rx_s;
  }
  csv << ',';
  if (r.write_pending == 0 && r.all_write_terminal_s > 0) {
    csv << r.all_write_terminal_s;
  }
  csv << ',';
  if (r.delivery_ok) {
    csv << r.goodput_mib_s;
  }
  csv << ',' << r.write_terminal_p50_ms << ',' << r.write_terminal_p95_ms << ','
      << r.write_to_rx_p50_ms << ',' << r.write_to_rx_p95_ms << ','
      << r.write_to_rx_max_ms << ',' << r.udp_max_tx << ',' << r.udp_max_rx
      << ',' << r.udp_tx_violations << ',' << r.udp_rx_violations << ','
      << (r.delivery_ok ? 1 : 0) << ',' << (r.budget_ok ? 1 : 0) << ',';
  if (r.valid_goodput) {
    csv << r.goodput_mib_s;
  }
  csv << ',' << r.udp_budget_status << ',' << (r.integrity_ok ? 1 : 0) << ','
      << r.outcome << ',' << sdk << '\n';
  csv.flush();
}

void WriteSetupFailedRow(std::ofstream& csv, PlannedPoint const& p, int repeat,
                         std::string const& reason, std::string const& sdk) {
  StreamingRunResult r{};
  r.transport = p.transport;
  r.stream = p.stream;
  r.mode = p.mode;
  r.chunk_bytes = p.chunk;
  r.outstanding_limit = p.outstanding;
  r.repeat = repeat;
  r.budget_ok = false;
  r.outcome = reason;
  WriteStreamingRow(csv, r, sdk);
}

std::vector<std::size_t> StreamingOutstandingMatrix(StreamMatrixBench const& bench) {
  auto outs = DefaultOutstandingMatrix();
  for (auto o : DefaultHighOutstandingMatrix()) {
    if (bench.GeneratorWriteBudgetOk(o)) {
      outs.push_back(o);
    }
  }
  return outs;
}

std::vector<PlannedPoint> BuildFullTcpPlan(StreamInfo const& info,
                                           std::vector<std::size_t> const& outs) {
  std::vector<PlannedPoint> plan;
  auto chunks = DefaultTcpChunkMatrix(info);
  for (auto c : chunks) {
    for (auto o : outs) {
      for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
        plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream,
                        mode, c, o, 1});
      }
    }
  }
  return plan;
}

std::vector<PlannedPoint> BuildReproPrefixPlan(StreamInfo const& info,
                                               std::size_t count,
                                               std::vector<std::size_t> const& outs) {
  auto full = BuildFullTcpPlan(info, outs);
  if (full.size() <= count) {
    return full;
  }
  return std::vector<PlannedPoint>(full.begin(), full.begin() + count);
}

void ApplyBudgetFlags(StreamingRunResult& result) {
  if (result.transport != BenchTransportKind::kUdp) {
    result.budget_ok = true;
    return;
  }
  UdpSocketAuditSnapshot snap{};
  snap.max_tx_payload_bytes = result.udp_max_tx;
  snap.max_rx_payload_bytes = result.udp_max_rx;
  snap.tx_violations = result.udp_tx_violations;
  snap.rx_violations = result.udp_rx_violations;
  snap.recv_error_events = result.udp_recv_error_events;
  snap.recv_emsgsize_events = result.udp_recv_emsgsize_events;
  snap.recv_emsgsize_budget_lower_bound =
      result.udp_recv_emsgsize_budget_lower_bound;
  snap.recv_other_errors = result.udp_recv_other_errors;
  snap.tx_observed = result.udp_tx_observed;
  snap.rx_observed = result.udp_rx_observed;

  auto const verdict = EvaluateUdpBudget(snap);
  result.budget_ok = verdict.budget_ok();
  result.valid_goodput = result.delivery_ok && verdict.budget_ok();
  result.udp_budget_status = UdpBudgetStatusName(verdict.status);
  auto const budget_outcome = ClassifyUdpBudgetOutcome(verdict);
  if (verdict.status == UdpBudgetStatus::kCompliant) {
    if (result.delivery_ok && (result.outcome.empty() || result.outcome == "OK")) {
      result.outcome = "OK";
    }
  } else if (!budget_outcome.empty()) {
    if (result.delivery_ok && verdict.status == UdpBudgetStatus::kViolated) {
      result.outcome = "OK_BUDGET_EXCEEDED";
    } else if (result.outcome == "OK" || result.outcome.empty()) {
      result.outcome = std::string(budget_outcome);
    } else if (result.delivery_ok) {
      result.outcome = std::string(budget_outcome);
    }
  }
}

struct BestPoint {
  std::size_t chunk;
  std::size_t outstanding;
  LoadMode mode;
  double goodput;
};

static std::optional<std::size_t> CsvColumnIndex(std::vector<std::string> const& header,
                                                 char const* name) {
  for (std::size_t i = 0; i < header.size(); ++i) {
    if (header[i] == name) {
      return i;
    }
  }
  return std::nullopt;
}

static bool CsvCellIsOne(std::vector<std::string> const& cols, std::size_t idx) {
  return idx < cols.size() && cols[idx] == "1";
}

std::vector<BestPoint> LoadTopConfigs(std::filesystem::path const& path,
                                      int top_n) {
  std::ifstream in(path);
  std::string line;
  if (!std::getline(in, line)) {
    return {};
  }
  std::stringstream header_ss(line);
  std::string cell;
  std::vector<std::string> header;
  while (std::getline(header_ss, cell, ',')) {
    header.push_back(cell);
  }
  auto const idx_block = CsvColumnIndex(header, "block_bytes");
  auto const idx_out = CsvColumnIndex(header, "outstanding");
  auto const idx_mode = CsvColumnIndex(header, "mode");
  auto const idx_goodput = CsvColumnIndex(header, "goodput_mib_s");
  auto const idx_delivery = CsvColumnIndex(header, "delivery_ok");
  auto const idx_integrity = CsvColumnIndex(header, "integrity_ok");
  auto const idx_trusted = CsvColumnIndex(header, "write_status_trusted");
  auto const idx_budget = CsvColumnIndex(header, "budget_ok");
  auto const idx_outcome = CsvColumnIndex(header, "outcome");
  if (!idx_block || !idx_out || !idx_mode || !idx_goodput) {
    return {};
  }
  std::map<std::tuple<std::size_t, std::size_t, std::string>, double> best;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    std::vector<std::string> cols;
    while (std::getline(ss, cell, ',')) {
      cols.push_back(cell);
    }
    if (cols.size() < header.size()) {
      continue;
    }
    if (idx_outcome && idx_outcome < cols.size() && cols[*idx_outcome] != "OK") {
      continue;
    }
    if (!CsvCellIsOne(cols, *idx_delivery) || !CsvCellIsOne(cols, *idx_integrity)) {
      continue;
    }
    if (idx_trusted && !CsvCellIsOne(cols, *idx_trusted)) {
      continue;
    }
    if (idx_budget && !CsvCellIsOne(cols, *idx_budget)) {
      continue;
    }
    try {
      auto chunk = static_cast<std::size_t>(std::stoull(cols[*idx_block]));
      auto outstanding = static_cast<std::size_t>(std::stoull(cols[*idx_out]));
      auto mode = cols[*idx_mode];
      double goodput = std::stod(cols[*idx_goodput]);
      if (goodput <= 0.0) {
        continue;
      }
      auto key = std::make_tuple(chunk, outstanding, mode);
      best[key] = std::max(best[key], goodput);
    } catch (...) {
    }
  }
  std::vector<BestPoint> ranked;
  for (auto const& [k, g] : best) {
    ranked.push_back({std::get<0>(k), std::get<1>(k), ParseLoadModeName(std::get<2>(k)), g});
  }
  std::sort(ranked.begin(), ranked.end(),
            [](BestPoint const& a, BestPoint const& b) {
              return a.goodput > b.goodput;
            });
  if (static_cast<int>(ranked.size()) > top_n) {
    ranked.resize(static_cast<std::size_t>(top_n));
  }
  return ranked;
}

}  // namespace

int StreamMatrixBench::ExecuteStreamingPlan(
    std::vector<PlannedPoint> plan, std::vector<std::uint8_t> const& blob,
    std::filesystem::path const& csv_path, bool append_csv, bool skip_completed) {
  auto const sdk = SdkSha(options_);
  std::set<PointKey> completed;
  if (skip_completed) {
    completed = LoadCompletedPointKeys(csv_path);
  }
  std::ofstream csv(csv_path, append_csv ? std::ios::app : std::ios::trunc);
  if (!append_csv) {
    WriteStreamingHeader(csv);
  }

  std::vector<StreamingRunResult> results;
  for (auto const& p : plan) {
    if (skip_completed && completed.count(PointKeyOf(p))) {
      log_stream_ << "POINT_SKIP chunk=" << p.chunk << " out=" << p.outstanding
                  << ' ' << LoadModeName(p.mode) << '\n';
      continue;
    }
    log_stream_ << "POINT_START transport=" << TransportKindName(p.transport)
                << " stream=" << StreamKindName(p.stream) << " chunk=" << p.chunk
                << " out=" << p.outstanding << ' ' << LoadModeName(p.mode) << '\n';

    auto receiver = std::make_shared<StreamingReceiverState>(
        blob, run_id_, p.payload_total_bytes);
    SetStreamingMeasureReceiver(receiver);

    auto ex = RunServiceExchangeLogged();
    if (!ex.ok) {
      log_stream_ << "SETUP_FAILED stage=" << ex.failure_stage << '\n';
      WriteSetupFailedRow(csv, p, 0, std::string("SETUP_FAILED:") + ex.failure_stage,
                          sdk);
      ++run_id_;
      continue;
    }
    write_status_subs_.Reset();
    ResetUdpSocketPayloadAudit();
    bool const udp_verbose_trace = options_.streaming_udp_diagnose ||
                                   options_.streaming_udp_four_block;
    if (p.transport == BenchTransportKind::kUdp && udp_verbose_trace) {
      auto const tag = PointArtifactTag(p, 0) + "_attempt" + std::to_string(run_id_);
      BeginUdpDatagramTrace(
          options_.output_dir / ("udp_datagram_trace_" + tag + ".csv"),
          UdpDatagramTraceOptions{.verbose = true, .transfer_id = run_id_});
    }

    auto const chunks_total =
        (p.payload_total_bytes + p.chunk - 1) / p.chunk;
    auto const point_deadline_duration = StreamingPointDeadline(options_);
    for (int rep = 0; rep < p.repeats; ++rep) {
      receiver->Reset(run_id_);
      BenchInstant const deadline = BenchNow() + point_deadline_duration;
      PointRunArgs args{
          .mode = p.mode,
          .stream_kind = p.stream,
          .chunk_bytes = p.chunk,
          .outstanding_limit = p.outstanding,
          .chunks_total = chunks_total,
          .run_id = run_id_,
          .blob = &blob,
          .payload_total_bytes = p.payload_total_bytes,
      };
      if (p.transport == BenchTransportKind::kUdp) {
        args.progress_log = true;
        auto const tag = PointArtifactTag(p, rep);
        args.progress_log_path = options_.output_dir / ("progress_" + tag + ".log");
        args.chunk_timeline_path =
            options_.output_dir / ("chunk_timeline_" + tag + ".csv");
      }
      auto result = RunOneStreamingPoint(*this, args, receiver, deadline);
      result.transport = p.transport;
      result.repeat = rep;
      auto audit = UdpSocketPayloadAuditSnapshot();
      result.udp_max_tx = audit.max_tx_payload_bytes;
      result.udp_max_rx = audit.max_rx_payload_bytes;
      result.udp_tx_violations = audit.tx_violations;
      result.udp_rx_violations = audit.rx_violations;
      result.udp_recv_error_events = audit.recv_error_events;
      result.udp_recv_emsgsize_events = audit.recv_emsgsize_events;
      result.udp_recv_emsgsize_budget_lower_bound =
          audit.recv_emsgsize_budget_lower_bound;
      result.udp_recv_other_errors = audit.recv_other_errors;
      result.udp_tx_observed = audit.tx_observed;
      result.udp_rx_observed = audit.rx_observed;
      ApplyBudgetFlags(result);
      results.push_back(result);
      WriteStreamingRow(csv, result, sdk);
      log_stream_ << "POINT_END chunk=" << p.chunk << " out=" << p.outstanding << ' '
                  << LoadModeName(p.mode) << " outcome=" << result.outcome
                  << " goodput_mib_s=" << result.goodput_mib_s
                  << " udp_max_rx=" << result.udp_max_rx
                  << " rx_viol=" << result.udp_rx_violations
                  << " recv_emsgsize=" << audit.recv_emsgsize_events << '\n';
      if (p.transport == BenchTransportKind::kUdp && udp_verbose_trace) {
        EndUdpDatagramTrace();
      }
      ++run_id_;
    }
  }
  return std::any_of(results.begin(), results.end(),
                     [](StreamingRunResult const& r) { return r.delivery_ok; })
             ? 0
             : 2;
}

int StreamMatrixBench::RunStreamingTcp() {
  log_stream_ << "streaming_tcp total_bytes=" << kThroughputTotalBytes << '\n';
  auto blob = BuildThroughputBlob();
  if (SetupMode(BenchStreamKind::kP2pStream, BenchTransportKind::kTcp) !=
      SetupResult::kOk) {
    return 1;
  }

  std::vector<PlannedPoint> plan;
  if (options_.streaming_tcp_smoke) {
    for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
      plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream, mode,
                      1024, 16, 1});
    }
  } else if (options_.streaming_repro_chunk && options_.streaming_repro_outstanding) {
    auto const outs = StreamingOutstandingMatrix(*this);
    plan = BuildReproPrefixPlan(send_stream_->stream_info(),
                                options_.streaming_repro_prefix_points, outs);
    LoadMode mode = options_.streaming_repro_mode.value_or(LoadMode::kBurst);
    plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream, mode,
                    *options_.streaming_repro_chunk,
                    *options_.streaming_repro_outstanding, 1});
  } else if (options_.streaming_tcp_matrix || options_.streaming_tcp_resume) {
    plan = BuildFullTcpPlan(send_stream_->stream_info(),
                            StreamingOutstandingMatrix(*this));
  }

  auto const csv_path = options_.output_dir / "tcp_streaming.csv";
  bool const append = false;
  bool const skip = options_.streaming_tcp_resume;
  int code = ExecuteStreamingPlan(std::move(plan), blob, csv_path, append, skip);
  TeardownStreams();
  app_->Exit(0);
  return code;
}

int StreamMatrixBench::RunStreamingUdpSafe() {
  log_stream_ << "streaming_udp_safestream total_bytes=" << kThroughputTotalBytes
              << '\n';
  if (!kSdkUdpEnabled) {
    log_stream_ << "UDP not compiled in\n";
    return 3;
  }
  auto blob = BuildThroughputBlob();
  if (SetupMode(BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp) !=
      SetupResult::kOk) {
    return 1;
  }

  std::size_t max_payload = 0;
  if (safe_doc_.max_element_size_reported > sizeof(ThptWireHeader)) {
    max_payload = safe_doc_.max_element_size_reported - sizeof(ThptWireHeader);
  }
  auto chunks = DefaultSafeChunkMatrix(max_payload);
  auto outs = StreamingOutstandingMatrix(*this);

  std::vector<PlannedPoint> plan;
  if (options_.streaming_udp_diagnose) {
    if (!options_.streaming_point_deadline_s) {
      options_.streaming_point_deadline_s = 180;
    }
    log_stream_ << "streaming_udp_diagnose chunk=1024 outstanding=1 burst deadline_s="
                << *options_.streaming_point_deadline_s << '\n';
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    LoadMode::kBurst, 1024, 1, 1});
  } else if (options_.streaming_udp_four_block) {
    if (!options_.streaming_point_deadline_s) {
      options_.streaming_point_deadline_s = 90;
    }
    log_stream_ << "streaming_udp_four_block payload=" << kUdpFourBlockTotalBytes
                << " deadline_s=" << *options_.streaming_point_deadline_s << '\n';
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    LoadMode::kBurst, 1024, 1, 1, kUdpFourBlockTotalBytes});
    for (int rep = 0; rep < 3; ++rep) {
      plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                      LoadMode::kBurst, 1024, 4, 1, kUdpFourBlockTotalBytes});
    }
  } else if (options_.streaming_udp_smoke) {
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    LoadMode::kBurst, 1024, 16, 1});
  } else if (options_.streaming_repro_chunk && options_.streaming_repro_outstanding) {
    LoadMode mode = options_.streaming_repro_mode.value_or(LoadMode::kBurst);
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    mode, *options_.streaming_repro_chunk,
                    *options_.streaming_repro_outstanding, 1});
  } else if (options_.streaming_udp_matrix) {
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    LoadMode::kBurst, 1024,
                    std::min<std::size_t>(512, outs.back()), 1});
    for (auto c : chunks) {
      for (auto o : outs) {
        for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
          if (c == 1024 && o == outs.back()) {
            continue;
          }
          plan.push_back({BenchTransportKind::kUdp,
                          BenchStreamKind::kP2pSafeStream, mode, c, o, 1});
        }
      }
    }
  }

  InstallUdpSocketPayloadAudit();
  auto const csv_path = options_.output_dir / "udp_safestream_streaming.csv";
  int code = ExecuteStreamingPlan(std::move(plan), blob, csv_path, false, false);
  TeardownStreams();
  app_->Exit(0);
  return code;
}

int StreamMatrixBench::RunStreamingBestRepeats() {
  log_stream_ << "streaming_best_repeats\n";
  auto blob = BuildThroughputBlob();
  int exit_code = 2;

  if (SetupMode(BenchStreamKind::kP2pStream, BenchTransportKind::kTcp) ==
      SetupResult::kOk) {
    auto tops = LoadTopConfigs(options_.output_dir / "tcp_streaming.csv", 3);
    std::vector<PlannedPoint> plan;
    for (auto const& t : tops) {
      plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream, t.mode,
                      t.chunk, t.outstanding, 3});
    }
    if (!plan.empty()) {
      auto path = options_.output_dir / "tcp_streaming_best_repeats.csv";
      int c = ExecuteStreamingPlan(plan, blob, path, false, false);
      exit_code = std::min(exit_code, c);
    }
    TeardownStreams();
  }

  if (kSdkUdpEnabled &&
      SetupMode(BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp) ==
          SetupResult::kOk) {
    auto tops =
        LoadTopConfigs(options_.output_dir / "udp_safestream_streaming.csv", 3);
    std::vector<PlannedPoint> plan;
    for (auto const& t : tops) {
      plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                      t.mode, t.chunk, t.outstanding, 3});
    }
    InstallUdpSocketPayloadAudit();
    if (!plan.empty()) {
      auto path = options_.output_dir / "udp_safestream_streaming_best_repeats.csv";
      int c = ExecuteStreamingPlan(plan, blob, path, false, false);
      exit_code = std::min(exit_code, c);
    }
    TeardownStreams();
  }

  app_->Exit(0);
  return exit_code;
}

void WriteStreamingComparison(std::filesystem::path const& output_dir) {
  (void)output_dir;
}

}  // namespace ae::stream_matrix_bench
