/*
 * Copyright 2026 Aethernet Inc.
 * Max-speed 1 MiB streaming (TCP/P2pStream, UDP/P2pSafeStream).
 */

#include "stream_matrix/bench_runner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "stream_matrix/bench_throughput_frame.h"
#include "stream_matrix/bench_transport.h"
#include "stream_matrix/bench_udp_budget.h"

namespace ae::stream_matrix_bench {
namespace {

inline constexpr std::chrono::seconds kPointDeadline{120};

char const* LoadModeName(LoadMode m) {
  return m == LoadMode::kBurst ? "burst" : "backpressure";
}

double DeltaMs(BenchInstant a, BenchInstant b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

double Percentile(std::vector<double> v, double pct) {
  if (v.empty()) {
    return 0.0;
  }
  std::sort(v.begin(), v.end());
  auto idx = static_cast<std::size_t>(
      std::ceil(pct / 100.0 * static_cast<double>(v.size())) - 1.0);
  idx = std::min(idx, v.size() - 1);
  return v[idx];
}

struct ChunkWriteRecord {
  std::uint64_t chunk_index{};
  std::size_t payload_bytes{};
  BenchInstant submit{};
  BenchInstant terminal{};
  int status{-1};
  bool terminal_set{false};
};

struct StreamingRunResult {
  BenchTransportKind transport{};
  LoadMode mode{};
  BenchStreamKind stream{};
  std::size_t chunk_bytes{};
  std::size_t outstanding_limit{};
  int repeat{};
  std::uint64_t chunks_total{};
  std::uint64_t write_attempts{};
  std::uint64_t write_ok{};
  std::uint64_t write_fail{};
  std::uint64_t write_stop{};
  std::uint64_t write_pending{};
  std::uint64_t backpressure_events{};
  std::uint64_t peak_outstanding_writes{};
  std::uint64_t peak_outstanding_bytes{};
  std::uint64_t dup_chunks{};
  std::size_t rx_unique_bytes{};
  bool delivery_ok{false};
  bool integrity_ok{false};
  bool timed_out{false};
  double submit_duration_s{};
  double first_rx_s{};
  double last_rx_s{};
  double all_write_terminal_s{};
  double goodput_mib_s{};
  double block_latency_p50_ms{};
  double block_latency_p95_ms{};
  double block_latency_p99_ms{};
  double block_latency_max_ms{};
  std::size_t udp_max_tx{};
  std::size_t udp_max_rx{};
  std::uint64_t udp_tx_violations{};
  std::uint64_t udp_rx_violations{};
  std::string outcome;
};

struct ActiveWrite {
  std::uint64_t chunk_index{};
  std::size_t bytes{};
};

class StreamingReceiver {
 public:
  StreamingReceiver(std::vector<std::uint8_t> const& blob, std::uint64_t run_id)
      : blob_{blob}, run_id_{run_id} {
    received_.assign(kThroughputTotalBytes, false);
  }

  void Feed(DataBuffer const& data, BenchInstant now) {
    std::size_t off = 0;
    while (off + sizeof(ThptWireHeader) <= data.size()) {
      ThptWireHeader hdr{};
      std::memcpy(&hdr, data.data() + off, sizeof(hdr));
      if (hdr.magic != kThptMagic) {
        break;
      }
      std::size_t const frame_len = sizeof(ThptWireHeader) + hdr.payload_len;
      if (off + frame_len > data.size()) {
        break;
      }
      std::vector<std::uint8_t> frame(data.data() + off,
                                      data.data() + off + frame_len);
      AcceptFrame(DataBuffer{std::move(frame)}, now);
      off += frame_len;
    }
  }

  void AcceptFrame(DataBuffer const& data, BenchInstant now) {
    auto hdr = ParseThptHeader(data);
    if (!hdr || hdr->run_id != run_id_) {
      return;
    }
    if (!VerifyThptChunk(data, blob_, run_id_)) {
      integrity_fail_ = true;
      return;
    }
    if (seen_chunks_.count(hdr->chunk_index)) {
      ++dup_chunks_;
      return;
    }
    seen_chunks_.insert(hdr->chunk_index);
    for (std::uint32_t i = 0; i < hdr->payload_len; ++i) {
      received_[hdr->byte_offset + i] = true;
    }
    if (!first_rx_.time_since_epoch().count()) {
      first_rx_ = now;
    }
    last_rx_ = now;
    bytes_unique_ += hdr->payload_len;
  }

  bool Complete() const {
    return bytes_unique_ == kThroughputTotalBytes &&
           std::all_of(received_.begin(), received_.end(), [](bool b) { return b; });
  }

  std::size_t bytes_unique() const { return bytes_unique_; }
  std::uint64_t dup_chunks() const { return dup_chunks_; }
  bool integrity_fail() const { return integrity_fail_; }
  BenchInstant first_rx() const { return first_rx_; }
  BenchInstant last_rx() const { return last_rx_; }

  void Reset(std::uint64_t run_id) {
    run_id_ = run_id;
    received_.assign(kThroughputTotalBytes, false);
    seen_chunks_.clear();
    bytes_unique_ = 0;
    dup_chunks_ = 0;
    integrity_fail_ = false;
    first_rx_ = {};
    last_rx_ = {};
  }

 private:
  std::vector<std::uint8_t> const& blob_;
  std::uint64_t run_id_;
  std::vector<bool> received_;
  std::unordered_set<std::uint64_t> seen_chunks_;
  std::size_t bytes_unique_{0};
  std::uint64_t dup_chunks_{0};
  bool integrity_fail_{false};
  BenchInstant first_rx_{};
  BenchInstant last_rx_{};
};

struct RunContext {
  StreamMatrixBench* bench{};
  ByteIStream* send_stream{};
  std::vector<std::uint8_t> const* blob{};
  std::uint64_t run_id{};
  std::size_t chunk_bytes{};
  std::size_t outstanding_limit{};
  LoadMode mode{};
  BenchStreamKind stream_kind{};
  StreamingReceiver* receiver{};
  std::vector<ChunkWriteRecord>* records{};
  std::uint64_t* backpressure{};
  std::uint64_t* peak_writes{};
  std::uint64_t* peak_bytes{};
  std::uint64_t chunks_total{};
  bool* slot_free{};
};

bool TrySubmitOne(RunContext& ctx, std::uint64_t chunk_index,
                  std::deque<ActiveWrite>& active, bool& immediate_fail) {
  if (chunk_index >= ctx.chunks_total) {
    return false;
  }
  if (active.size() >= ctx.outstanding_limit) {
    return false;
  }
  auto const byte_offset = chunk_index * ctx.chunk_bytes;
  auto const remaining = kThroughputTotalBytes - byte_offset;
  auto const payload_len = std::min(ctx.chunk_bytes, remaining);
  ChunkWriteRecord rec{
      .chunk_index = chunk_index,
      .payload_bytes = payload_len,
      .submit = BenchNow(),
  };
  ctx.records->push_back(rec);
  auto wire = MakeThptChunk(*ctx.blob, ctx.run_id, chunk_index, byte_offset,
                            payload_len);
  auto& wa = ctx.send_stream->Write(std::move(wire));
  ctx.bench->WriteStatusSubs() += wa.status_event().Subscribe(
      [&, chunk_index](WriteAction::Status st) {
        for (auto& r : *ctx.records) {
          if (r.chunk_index == chunk_index && !r.terminal_set) {
            r.terminal = BenchNow();
            r.terminal_set = true;
            if (st == WriteAction::Status::kSuccess) {
              r.status = 0;
            } else if (st == WriteAction::Status::kFail) {
              r.status = 1;
            } else {
              r.status = 2;
            }
            break;
          }
        }
        if (ctx.mode == LoadMode::kBackpressure) {
          *ctx.slot_free = true;
        }
      });
  active.push_back(ActiveWrite{.chunk_index = chunk_index, .bytes = payload_len});
  if (wa.is_finished()) {
    ctx.bench->BenchPump(std::chrono::milliseconds{0});
    for (auto const& r : *ctx.records) {
      if (r.chunk_index == chunk_index && r.terminal_set && r.status == 1) {
        immediate_fail = true;
        ++(*ctx.backpressure);
        break;
      }
    }
  }
  return true;
}

StreamingRunResult RunOnePoint(RunContext base, BenchInstant point_deadline) {
  StreamingRunResult out{
      .mode = base.mode,
      .stream = base.stream_kind,
      .chunk_bytes = base.chunk_bytes,
      .outstanding_limit = base.outstanding_limit,
      .chunks_total = base.chunks_total,
  };

  std::vector<ChunkWriteRecord> records;
  std::deque<ActiveWrite> active;
  std::uint64_t next_chunk = 0;
  std::uint64_t backpressure = 0;
  std::uint64_t peak_writes = 0;
  std::uint64_t peak_bytes = 0;
  bool slot_free = true;

  RunContext ctx = base;
  ctx.records = &records;
  ctx.backpressure = &backpressure;
  ctx.peak_writes = &peak_writes;
  ctx.peak_bytes = &peak_bytes;
  ctx.slot_free = &slot_free;

  BenchInstant submit_begin{};
  BenchInstant submit_end{};
  BenchInstant all_terminal{};
  bool have_all_terminal = false;

  auto pump = [&]() { ctx.bench->BenchPump(std::chrono::milliseconds{1}); };

  auto drain_finished = [&]() {
    for (auto it = active.begin(); it != active.end();) {
      bool done = false;
      for (auto const& r : records) {
        if (r.chunk_index == it->chunk_index && r.terminal_set) {
          done = true;
          break;
        }
      }
      if (!done) {
        ++it;
        continue;
      }
      it = active.erase(it);
    }
  };

  auto update_peaks = [&]() {
    peak_writes = std::max(peak_writes, active.size());
    std::uint64_t ob = 0;
    for (auto const& a : active) {
      ob += a.bytes;
    }
    peak_bytes = std::max(peak_bytes, ob);
  };

  while (BenchNow() < point_deadline) {
    bool progressed = false;
    if (ctx.mode == LoadMode::kBurst) {
      while (next_chunk < ctx.chunks_total &&
             active.size() < ctx.outstanding_limit &&
             BenchNow() < point_deadline) {
        bool fail = false;
        if (!TrySubmitOne(ctx, next_chunk, active, fail)) {
          break;
        }
        if (submit_begin.time_since_epoch().count() == 0) {
          submit_begin = records.back().submit;
        }
        submit_end = records.back().submit;
        ++next_chunk;
        ++out.write_attempts;
        progressed = true;
        if (fail) {
          break;
        }
      }
    } else if (slot_free || active.size() < ctx.outstanding_limit) {
      slot_free = false;
      while (next_chunk < ctx.chunks_total &&
             active.size() < ctx.outstanding_limit &&
             BenchNow() < point_deadline) {
        bool fail = false;
        if (!TrySubmitOne(ctx, next_chunk, active, fail)) {
          break;
        }
        if (submit_begin.time_since_epoch().count() == 0) {
          submit_begin = records.back().submit;
        }
        submit_end = records.back().submit;
        ++next_chunk;
        ++out.write_attempts;
        progressed = true;
        if (fail) {
          break;
        }
        if (ctx.mode == LoadMode::kBackpressure) {
          break;
        }
      }
    }

    pump();
    drain_finished();
    update_peaks();

    if (next_chunk >= ctx.chunks_total && active.empty() &&
        ctx.receiver->Complete()) {
      break;
    }
  }

  out.timed_out = BenchNow() >= point_deadline && !ctx.receiver->Complete();

  for (auto const& r : records) {
    if (r.terminal_set) {
      if (r.status == 0) {
        ++out.write_ok;
      } else if (r.status == 1) {
        ++out.write_fail;
      } else if (r.status == 2) {
        ++out.write_stop;
      }
      if (!have_all_terminal) {
        all_terminal = r.terminal;
        have_all_terminal = true;
      } else if (r.terminal > all_terminal) {
        all_terminal = r.terminal;
      }
    } else {
      ++out.write_pending;
    }
  }

  out.backpressure_events = backpressure;
  out.peak_outstanding_writes = peak_writes;
  out.peak_outstanding_bytes = peak_bytes;
  out.dup_chunks = ctx.receiver->dup_chunks();
  out.rx_unique_bytes = ctx.receiver->bytes_unique();
  out.integrity_ok = !ctx.receiver->integrity_fail() &&
                     ctx.receiver->bytes_unique() == kThroughputTotalBytes &&
                     ctx.receiver->Complete();
  out.delivery_ok = out.integrity_ok;

  out.submit_duration_s = DeltaMs(submit_begin, submit_end) / 1000.0;
  if (ctx.receiver->first_rx().time_since_epoch().count()) {
    out.first_rx_s = DeltaMs(submit_begin, ctx.receiver->first_rx()) / 1000.0;
    out.last_rx_s = DeltaMs(submit_begin, ctx.receiver->last_rx()) / 1000.0;
    if (out.delivery_ok && out.last_rx_s > 0.0) {
      out.goodput_mib_s =
          (static_cast<double>(kThroughputTotalBytes) / (1024.0 * 1024.0)) /
          out.last_rx_s;
    }
  }
  if (have_all_terminal) {
    out.all_write_terminal_s = DeltaMs(submit_begin, all_terminal) / 1000.0;
  }

  std::vector<double> lat_ms;
  for (auto const& r : records) {
    if (r.terminal_set) {
      lat_ms.push_back(DeltaMs(r.submit, r.terminal));
    }
  }
  out.block_latency_p50_ms = Percentile(lat_ms, 50);
  out.block_latency_p95_ms = Percentile(lat_ms, 95);
  out.block_latency_p99_ms = Percentile(lat_ms, 99);
  out.block_latency_max_ms =
      lat_ms.empty() ? 0.0 : *std::max_element(lat_ms.begin(), lat_ms.end());

  if (out.delivery_ok) {
    out.outcome = "OK";
  } else if (out.timed_out) {
    out.outcome = "TIMEOUT";
  } else if (out.write_fail || out.write_stop) {
    out.outcome = "WRITE_ERRORS";
  } else if (ctx.receiver->bytes_unique() < kThroughputTotalBytes) {
    out.outcome = "INCOMPLETE";
  } else {
    out.outcome = "INTEGRITY_FAIL";
  }
  return out;
}

void WriteStreamingHeader(std::ofstream& csv) {
  csv << "transport,stream,mode,block_bytes,outstanding,repeat,chunks_total,"
         "write_attempts,write_ok,write_fail,write_stop,write_pending,"
         "backpressure_events,peak_outstanding_writes,peak_outstanding_bytes,"
         "dup_chunks,rx_unique_bytes,submit_duration_s,first_rx_s,last_rx_s,"
         "all_write_terminal_s,goodput_mib_s,block_lat_p50_ms,block_lat_p95_ms,"
         "block_lat_p99_ms,block_lat_max_ms,udp_max_tx,udp_max_rx,"
         "udp_tx_violations,udp_rx_violations,delivery_ok,integrity_ok,outcome,"
         "sdk_sha\n";
}

void WriteStreamingRow(std::ofstream& csv, StreamingRunResult const& r,
                       std::string const& sdk) {
  csv << TransportKindName(r.transport) << ',' << StreamKindName(r.stream) << ','
      << LoadModeName(r.mode) << ',' << r.chunk_bytes << ','
      << r.outstanding_limit << ',' << r.repeat << ',' << r.chunks_total << ','
      << r.write_attempts << ',' << r.write_ok << ',' << r.write_fail << ','
      << r.write_stop << ',' << r.write_pending << ',' << r.backpressure_events
      << ',' << r.peak_outstanding_writes << ',' << r.peak_outstanding_bytes
      << ',' << r.dup_chunks << ',' << r.rx_unique_bytes << ','
      << r.submit_duration_s << ',';
  if (r.first_rx_s > 0) {
    csv << r.first_rx_s;
  }
  csv << ',';
  if (r.last_rx_s > 0) {
    csv << r.last_rx_s;
  }
  csv << ',';
  if (r.all_write_terminal_s > 0) {
    csv << r.all_write_terminal_s;
  }
  csv << ',';
  if (r.delivery_ok) {
    csv << r.goodput_mib_s;
  }
  csv << ',' << r.block_latency_p50_ms << ',' << r.block_latency_p95_ms << ','
      << r.block_latency_p99_ms << ',' << r.block_latency_max_ms << ','
      << r.udp_max_tx << ',' << r.udp_max_rx << ',' << r.udp_tx_violations
      << ',' << r.udp_rx_violations << ',' << (r.delivery_ok ? 1 : 0) << ','
      << (r.integrity_ok ? 1 : 0) << ',' << r.outcome << ',' << sdk << '\n';
  csv.flush();
}

struct PlannedPoint {
  BenchTransportKind transport;
  BenchStreamKind stream;
  LoadMode mode;
  std::size_t chunk;
  std::size_t outstanding;
  int repeats;
};

std::string SdkSha(BenchCliOptions const& opt) {
  std::string sdk = opt.sdk_sha;
  if (opt.sdk_tree_dirty) {
    sdk += "-dirty";
  }
  return sdk;
}

}  // namespace

int StreamMatrixBench::RunStreamingTcp() {
  log_stream_ << "streaming_tcp total_bytes=" << kThroughputTotalBytes << '\n';
  auto blob = BuildThroughputBlob();
  auto const sdk = SdkSha(options_);
  bool const append = options_.streaming_best_repeats;
  std::ofstream csv(options_.output_dir / "tcp_streaming.csv",
                    append ? std::ios::app : std::ios::trunc);
  if (!append) {
    WriteStreamingHeader(csv);
  }

  std::vector<PlannedPoint> plan;
  if (SetupMode(BenchStreamKind::kP2pStream, BenchTransportKind::kTcp) !=
      SetupResult::kOk) {
    return 1;
  }
  auto chunks = DefaultTcpChunkMatrix(send_stream_->stream_info());
  auto outs = DefaultOutstandingMatrix();

  if (options_.streaming_tcp_smoke) {
    for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
      plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream,
                      mode, 1024, 16, 1});
    }
  } else if (options_.streaming_tcp_matrix || options_.streaming_best_repeats) {
    for (auto c : chunks) {
      for (auto o : outs) {
        for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
          plan.push_back({BenchTransportKind::kTcp, BenchStreamKind::kP2pStream,
                          mode, c, o, 1});
        }
      }
    }
  }

  std::vector<StreamingRunResult> results;
  Subscription recv_sub;
  StreamingReceiver* recv_ptr = nullptr;

  for (auto const& p : plan) {
    StreamingReceiver receiver(blob, run_id_);
    recv_ptr = &receiver;
    recv_sub.Reset();
    recv_sub = p2p_b_->out_data_event().Subscribe(
        [this, recv_ptr](DataBuffer const& data) {
          OnReceived(data);
          recv_ptr->Feed(data, BenchNow());
        });
    if (!RunServiceExchange()) {
      log_stream_ << "streaming service exchange failed\n";
      continue;
    }
    write_status_subs_.Reset();
    ResetUdpSocketPayloadAudit();

    auto const chunks_total = (kThroughputTotalBytes + p.chunk - 1) / p.chunk;
    for (int rep = 0; rep < p.repeats; ++rep) {
      BenchInstant const deadline = BenchNow() + kPointDeadline;
      RunContext ctx{
          .bench = this,
          .send_stream = send_stream_,
          .blob = &blob,
          .run_id = run_id_,
          .chunk_bytes = p.chunk,
          .outstanding_limit = p.outstanding,
          .mode = p.mode,
          .stream_kind = p.stream,
          .receiver = &receiver,
          .chunks_total = chunks_total,
      };
      auto result = RunOnePoint(ctx, deadline);
      result.transport = p.transport;
      result.repeat = rep;
      auto audit = UdpSocketPayloadAuditSnapshot();
      result.udp_max_tx = audit.max_tx_payload_bytes;
      result.udp_max_rx = audit.max_rx_payload_bytes;
      result.udp_tx_violations = audit.tx_violations;
      result.udp_rx_violations = audit.rx_violations;
      results.push_back(result);
      WriteStreamingRow(csv, result, sdk);
      log_stream_ << "tcp_streaming chunk=" << p.chunk << " out=" << p.outstanding
                  << ' ' << LoadModeName(p.mode) << " outcome=" << result.outcome
                  << " goodput_mib_s=" << result.goodput_mib_s << '\n';
      receiver.Reset(run_id_);
      ++run_id_;
    }
  }

  TeardownStreams();
  app_->Exit(0);
  return std::any_of(results.begin(), results.end(),
                     [](StreamingRunResult const& r) { return r.delivery_ok; })
             ? 0
             : 2;
}

int StreamMatrixBench::RunStreamingUdpSafe() {
  log_stream_ << "streaming_udp_safestream total_bytes=" << kThroughputTotalBytes
              << '\n';
  if (!kSdkUdpEnabled) {
    log_stream_ << "UDP not compiled in\n";
    return 3;
  }
  auto blob = BuildThroughputBlob();
  auto const sdk = SdkSha(options_);
  bool const append = options_.streaming_best_repeats;
  std::ofstream csv(options_.output_dir / "udp_safestream_streaming.csv",
                    append ? std::ios::app : std::ios::trunc);
  if (!append) {
    WriteStreamingHeader(csv);
  }

  if (SetupMode(BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp) !=
      SetupResult::kOk) {
    return 1;
  }

  std::size_t max_payload = 0;
  if (safe_doc_.max_element_size_reported > sizeof(ThptWireHeader)) {
    max_payload = safe_doc_.max_element_size_reported - sizeof(ThptWireHeader);
  }
  auto chunks = DefaultSafeChunkMatrix(max_payload);
  auto outs = DefaultOutstandingMatrix();

  std::vector<PlannedPoint> plan;
  if (options_.streaming_udp_smoke) {
    plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                    LoadMode::kBurst, 1024, 16, 1});
    if (max_payload > 1200) {
      plan.push_back({BenchTransportKind::kUdp, BenchStreamKind::kP2pSafeStream,
                      LoadMode::kBurst, std::min(max_payload, std::size_t{2048}),
                      4, 1});
    }
  } else if (options_.streaming_udp_matrix || options_.streaming_best_repeats) {
    for (auto c : chunks) {
      for (auto o : outs) {
        for (auto mode : {LoadMode::kBurst, LoadMode::kBackpressure}) {
          plan.push_back({BenchTransportKind::kUdp,
                          BenchStreamKind::kP2pSafeStream, mode, c, o, 1});
        }
      }
    }
  }

  InstallUdpSocketPayloadAudit();
  std::vector<StreamingRunResult> results;
  Subscription recv_sub;
  StreamingReceiver* recv_ptr = nullptr;

  for (auto const& p : plan) {
    StreamingReceiver receiver(blob, run_id_);
    recv_ptr = &receiver;
    recv_sub.Reset();
    recv_sub_a_.Reset();
    if (safe_b_) {
      recv_sub = safe_b_->out_data_event().Subscribe(
          [this, recv_ptr](DataBuffer const& data) {
            OnReceived(data);
            recv_ptr->Feed(data, BenchNow());
          });
    }
    if (!RunServiceExchange()) {
      log_stream_ << "udp streaming service exchange failed\n";
      continue;
    }
    write_status_subs_.Reset();
    ResetUdpSocketPayloadAudit();

    auto const chunks_total = (kThroughputTotalBytes + p.chunk - 1) / p.chunk;
    for (int rep = 0; rep < p.repeats; ++rep) {
      BenchInstant const deadline = BenchNow() + kPointDeadline;
      RunContext ctx{
          .bench = this,
          .send_stream = send_stream_,
          .blob = &blob,
          .run_id = run_id_,
          .chunk_bytes = p.chunk,
          .outstanding_limit = p.outstanding,
          .mode = p.mode,
          .stream_kind = p.stream,
          .receiver = &receiver,
          .chunks_total = chunks_total,
      };
      auto result = RunOnePoint(ctx, deadline);
      result.transport = p.transport;
      result.repeat = rep;
      auto audit = UdpSocketPayloadAuditSnapshot();
      result.udp_max_tx = audit.max_tx_payload_bytes;
      result.udp_max_rx = audit.max_rx_payload_bytes;
      result.udp_tx_violations = audit.tx_violations;
      result.udp_rx_violations = audit.rx_violations;
      if (audit.tx_violations || audit.rx_violations) {
        log_stream_ << "udp audit tx_max=" << audit.max_tx_payload_bytes
                    << " rx_max=" << audit.max_rx_payload_bytes
                    << " violations tx=" << audit.tx_violations
                    << " rx=" << audit.rx_violations << '\n';
      }
      results.push_back(result);
      WriteStreamingRow(csv, result, sdk);
      log_stream_ << "udp_streaming chunk=" << p.chunk << " out=" << p.outstanding
                  << ' ' << LoadModeName(p.mode) << " outcome=" << result.outcome
                  << " goodput_mib_s=" << result.goodput_mib_s << '\n';
      receiver.Reset(run_id_);
      ++run_id_;
    }
  }

  TeardownStreams();
  app_->Exit(0);
  return std::any_of(results.begin(), results.end(),
                     [](StreamingRunResult const& r) { return r.delivery_ok; })
             ? 0
             : 2;
}

int StreamMatrixBench::RunStreamingBestRepeats() {
  options_.streaming_best_repeats = true;
  return RunStreamingTcp();
}

void WriteStreamingComparison(std::filesystem::path const& output_dir) {
  (void)output_dir;
}

}  // namespace ae::stream_matrix_bench
