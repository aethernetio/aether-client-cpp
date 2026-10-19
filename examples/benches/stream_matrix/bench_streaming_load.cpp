/*
 * Copyright 2026 Aethernet Inc.
 */
#include "stream_matrix/bench_streaming_load.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include "aether/events/events.h"
#include "aether/write_action/failed_write_action.h"
#include "aether/write_action/write_action.h"

#include "stream_matrix/bench_throughput_frame.h"

namespace ae::stream_matrix_bench {
namespace {

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

constexpr int kBenchStatusSuccess = 0;
constexpr int kBenchStatusFail = 1;
constexpr int kBenchStatusStop = 2;
constexpr int kBenchStatusUnknown = 3;

int MapWriteStatus(WriteAction::Status st) {
  switch (st) {
    case WriteAction::Status::kSuccess:
      return kBenchStatusSuccess;
    case WriteAction::Status::kFail:
      return kBenchStatusFail;
    case WriteAction::Status::kStop:
      return kBenchStatusStop;
  }
  return kBenchStatusUnknown;
}

struct ChunkWriteRecord {
  std::uint64_t chunk_index{};
  std::size_t payload_bytes{};
  BenchInstant submit{};
  BenchInstant terminal{};
  int status{-1};
  bool terminal_set{false};
};

struct ChunkTerminalSlot {
  bool terminal_set{false};
  BenchInstant terminal{};
  int status{-1};
};

struct InFlightWrite {
  std::uint64_t chunk_index{};
  std::size_t bytes{};
  Subscription status_sub{};
};

struct PointSession : std::enable_shared_from_this<PointSession> {
  StreamMatrixBench* bench{};
  ByteIStream* send_stream{};
  std::vector<std::uint8_t> const* blob{};
  std::shared_ptr<StreamingReceiverState> receiver;
  std::uint64_t run_id{};
  std::size_t chunk_bytes{};
  std::size_t outstanding_limit{};
  LoadMode mode{};
  std::uint64_t chunks_total{};
  std::size_t payload_total_bytes{kThroughputTotalBytes};
  std::atomic<bool> accept_submits{true};
  std::atomic<bool> slot_available{true};
  std::uint64_t write_calls{0};
  std::uint64_t write_accepted{0};
  std::uint64_t generator_limit_events{0};
  std::uint64_t sdk_capacity_events{0};
  std::uint64_t write_fail_events{0};
  std::vector<ChunkWriteRecord> records;
  std::vector<ChunkTerminalSlot> terminals;
  std::deque<InFlightWrite> in_flight;

  void note_peaks(std::uint64_t& peak_writes, std::uint64_t& peak_bytes) {
    peak_writes = std::max(peak_writes, in_flight.size());
    std::uint64_t ob = 0;
    for (auto const& a : in_flight) {
      ob += a.bytes;
    }
    peak_bytes = std::max(peak_bytes, ob);
  }

  void mark_terminal(std::uint64_t chunk_index, int bench_status) {
    if (chunk_index >= terminals.size()) {
      return;
    }
    auto& slot = terminals[chunk_index];
    if (slot.terminal_set) {
      return;
    }
    slot.terminal = BenchNow();
    slot.terminal_set = true;
    slot.status = bench_status;
    if (bench_status == kBenchStatusFail) {
      ++write_fail_events;
    }
  }

  void remove_in_flight(std::uint64_t chunk_index) {
    for (auto it = in_flight.begin(); it != in_flight.end(); ++it) {
      if (it->chunk_index == chunk_index) {
        it->status_sub.Reset();
        in_flight.erase(it);
        return;
      }
    }
  }

  void on_write_status(std::uint64_t chunk_index, WriteAction::Status st) {
    remove_in_flight(chunk_index);
    mark_terminal(chunk_index, MapWriteStatus(st));
    slot_available.store(true);
  }

  void on_write_unknown(std::uint64_t chunk_index) {
    if (chunk_index >= terminals.size() || terminals[chunk_index].terminal_set) {
      return;
    }
    remove_in_flight(chunk_index);
    mark_terminal(chunk_index, kBenchStatusUnknown);
    slot_available.store(true);
  }

  void sync_record_terminals() {
    for (auto& r : records) {
      if (r.chunk_index >= terminals.size()) {
        continue;
      }
      auto const& slot = terminals[r.chunk_index];
      if (!slot.terminal_set) {
        continue;
      }
      r.terminal = slot.terminal;
      r.terminal_set = true;
      r.status = slot.status;
    }
  }

  void prune_in_flight() {
    for (auto it = in_flight.begin(); it != in_flight.end();) {
      if (it->chunk_index < terminals.size() &&
          terminals[it->chunk_index].terminal_set) {
        it->status_sub.Reset();
        it = in_flight.erase(it);
      } else {
        ++it;
      }
    }
  }

  enum class SubmitOutcome {
    kSubmitted,
    kOutstandingCap,
    kAllSubmitted,
    kWriteFail,
    kResourceCap,
  };

  SubmitOutcome try_submit(std::uint64_t chunk_index, std::uint64_t& peak_writes,
                           std::uint64_t& peak_bytes) {
    if (!accept_submits.load()) {
      return SubmitOutcome::kAllSubmitted;
    }
    if (chunk_index >= chunks_total) {
      return SubmitOutcome::kAllSubmitted;
    }
    if (in_flight.size() >= outstanding_limit) {
      return SubmitOutcome::kOutstandingCap;
    }
    if (!bench->GeneratorWriteBudgetOk(in_flight.size() + 1)) {
      ++generator_limit_events;
      return SubmitOutcome::kResourceCap;
    }
    auto const byte_offset = chunk_index * chunk_bytes;
    auto const remaining = payload_total_bytes - byte_offset;
    auto const payload_len = std::min(chunk_bytes, remaining);
    ++write_calls;
    auto const submit_time = BenchNow();
    auto wire = MakeThptChunk(*blob, run_id, chunk_index, byte_offset, payload_len);
    auto& wa = send_stream->Write(std::move(wire));
    if (dynamic_cast<FailedWriteAction*>(&wa) != nullptr) {
      ++write_fail_events;
      return SubmitOutcome::kWriteFail;
    }
    ++write_accepted;
    records.push_back(ChunkWriteRecord{
        .chunk_index = chunk_index,
        .payload_bytes = payload_len,
        .submit = submit_time,
    });
    InFlightWrite flight{.chunk_index = chunk_index, .bytes = payload_len};
    std::weak_ptr<PointSession> weak = shared_from_this();
    flight.status_sub = wa.status_event().Subscribe(
        [weak, chunk_index](WriteAction::Status st) {
          if (auto self = weak.lock()) {
            self->on_write_status(chunk_index, st);
          }
        });
    in_flight.push_back(std::move(flight));
    note_peaks(peak_writes, peak_bytes);
    if (wa.is_finished()) {
      bench->ScheduleAfterPump([weak, chunk_index]() {
        if (auto self = weak.lock()) {
          if (chunk_index < self->terminals.size() &&
              !self->terminals[chunk_index].terminal_set) {
            self->on_write_unknown(chunk_index);
          }
        }
      });
    }
    return SubmitOutcome::kSubmitted;
  }
};

void UpdateEventPeaks(StreamMatrixBench& bench, StreamingRunResult& out) {
  auto const u = bench.EventPoolUsageNow();
  out.events_registered_peak =
      std::max(out.events_registered_peak, u.registered_events);
  out.handlers_active_peak =
      std::max(out.handlers_active_peak, u.handler_relationships);
}

struct LiveWriteCounts {
  int ok{0};
  int fail{0};
  int stop{0};
  int unknown{0};
  int pending{0};
};

LiveWriteCounts CountWriteStatuses(PointSession const& session) {
  LiveWriteCounts c{};
  for (auto const& r : session.records) {
    if (r.chunk_index >= session.terminals.size() ||
        !session.terminals[r.chunk_index].terminal_set) {
      ++c.pending;
      continue;
    }
    switch (session.terminals[r.chunk_index].status) {
      case kBenchStatusSuccess:
        ++c.ok;
        break;
      case kBenchStatusFail:
        ++c.fail;
        break;
      case kBenchStatusStop:
        ++c.stop;
        break;
      default:
        ++c.unknown;
        break;
    }
  }
  return c;
}

void WriteChunkTimeline(std::filesystem::path const& path, BenchInstant point_start,
                        PointSession const& session,
                        StreamingReceiverState const& receiver) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return;
  }
  out << "chunk_index,write_ms,rx_ms,terminal_ms,status\n";
  for (auto const& r : session.records) {
    double write_ms = DeltaMs(point_start, r.submit);
    double rx_ms = 0.0;
    auto rx = receiver.recv_time_for_chunk(r.chunk_index);
    if (rx.time_since_epoch().count()) {
      rx_ms = DeltaMs(point_start, rx);
    }
    double term_ms = 0.0;
    int status = -1;
    if (r.terminal_set) {
      term_ms = DeltaMs(point_start, r.terminal);
      status = r.status;
    } else if (r.chunk_index < session.terminals.size() &&
               session.terminals[r.chunk_index].terminal_set) {
      term_ms = DeltaMs(point_start, session.terminals[r.chunk_index].terminal);
      status = session.terminals[r.chunk_index].status;
    }
    out << r.chunk_index << ',' << write_ms << ',' << rx_ms << ',' << term_ms
        << ',' << status << '\n';
  }
}

void MaybeLogProgress(StreamMatrixBench& bench, PointRunArgs const& args,
                      BenchInstant point_start, BenchInstant now,
                      BenchInstant& last_progress_at, PointSession const& session,
                      StreamingReceiverState const& receiver,
                      std::uint64_t next_chunk_index) {
  if (!args.progress_log) {
    return;
  }
  if (last_progress_at.time_since_epoch().count() &&
      now - last_progress_at < std::chrono::seconds{1}) {
    return;
  }
  last_progress_at = now;
  auto const counts = CountWriteStatuses(session);
  double last_rx_age_ms = -1.0;
  if (receiver.last_rx().time_since_epoch().count()) {
    last_rx_age_ms = DeltaMs(receiver.last_rx(), now);
  }
  double last_terminal_age_ms = -1.0;
  BenchInstant last_term{};
  for (auto const& slot : session.terminals) {
    if (slot.terminal_set && slot.terminal > last_term) {
      last_term = slot.terminal;
    }
  }
  if (last_term.time_since_epoch().count()) {
    last_terminal_age_ms = DeltaMs(last_term, now);
  }
  std::ostringstream os;
  os << "PROGRESS elapsed_s=" << (DeltaMs(point_start, now) / 1000.0)
     << " write_calls=" << session.write_calls
     << " write_accepted=" << session.write_accepted
     << " rx_chunks=" << receiver.received_chunk_count()
     << " rx_bytes=" << receiver.bytes_unique() << " write_ok=" << counts.ok
     << " write_fail=" << counts.fail << " write_stop=" << counts.stop
     << " write_pending=" << counts.pending << " write_unknown=" << counts.unknown
     << " last_rx_age_ms=" << last_rx_age_ms
     << " last_terminal_age_ms=" << last_terminal_age_ms
     << " in_flight=" << session.in_flight.size()
     << " next_chunk=" << next_chunk_index;
  bench.AppendRunLog(os.str());
  if (!args.progress_log_path.empty()) {
    std::ofstream pl(args.progress_log_path, std::ios::app);
    if (pl) {
      pl << os.str() << '\n';
    }
  }
}

bool PointFullyResolved(PointSession const& session,
                        StreamingReceiverState const& receiver) {
  if (!receiver.Complete()) {
    return false;
  }
  if (!session.in_flight.empty()) {
    return false;
  }
  for (std::size_t i = 0; i < session.records.size(); ++i) {
    if (i >= session.terminals.size() || !session.terminals[i].terminal_set) {
      return false;
    }
  }
  return true;
}

}  // namespace

StreamingRunResult RunOneStreamingPoint(StreamMatrixBench& bench, PointRunArgs args,
                                      std::shared_ptr<StreamingReceiverState> receiver,
                                      BenchInstant point_deadline) {
  StreamingRunResult out{};
  out.mode = args.mode;
  out.stream = args.stream_kind;
  out.chunk_bytes = args.chunk_bytes;
  out.outstanding_limit = args.outstanding_limit;
  out.chunks_total = args.chunks_total;
  out.payload_total_bytes = args.payload_total_bytes;
  out.write_status_trusted = true;

  {
    auto const u = bench.EventPoolUsageNow();
    out.events_registered_start = u.registered_events;
    out.handlers_active_start = u.handler_relationships;
    out.events_registered_peak = u.registered_events;
    out.handlers_active_peak = u.handler_relationships;
  }

  auto session = std::make_shared<PointSession>();
  session->bench = &bench;
  session->send_stream = bench.SendStream();
  session->blob = args.blob;
  session->receiver = std::move(receiver);
  session->run_id = args.run_id;
  session->chunk_bytes = args.chunk_bytes;
  session->outstanding_limit = args.outstanding_limit;
  session->mode = args.mode;
  session->chunks_total = args.chunks_total;
  session->payload_total_bytes = args.payload_total_bytes;
  session->records.reserve(args.chunks_total);
  session->terminals.clear();
  session->terminals.resize(args.chunks_total);

  std::uint64_t next_chunk = 0;
  std::uint64_t peak_writes = 0;
  std::uint64_t peak_bytes = 0;
  BenchInstant submit_begin{};
  BenchInstant submit_end{};
  BenchInstant all_terminal{};
  bool have_all_terminal = false;

  auto try_fill = [&]() -> bool {
    bool any = false;
    if (args.mode == LoadMode::kBurst) {
      while (next_chunk < args.chunks_total && BenchNow() < point_deadline) {
        if (session->in_flight.size() >= args.outstanding_limit) {
          break;
        }
        auto r = session->try_submit(next_chunk, peak_writes, peak_bytes);
        if (r == PointSession::SubmitOutcome::kOutstandingCap ||
            r == PointSession::SubmitOutcome::kAllSubmitted ||
            r == PointSession::SubmitOutcome::kResourceCap) {
          break;
        }
        if (r == PointSession::SubmitOutcome::kWriteFail) {
          ++out.write_fail;
          ++out.write_attempts;
          break;
        }
        if (submit_begin.time_since_epoch().count() == 0) {
          submit_begin = session->records.back().submit;
        }
        submit_end = session->records.back().submit;
        ++next_chunk;
        ++out.write_attempts;
        any = true;
        bench.BenchPump(std::chrono::milliseconds{0});
        session->sync_record_terminals();
        session->prune_in_flight();
        UpdateEventPeaks(bench, out);
      }
      return any;
    }
    any = false;
    if (!session->slot_available.load() &&
        session->in_flight.size() >= args.outstanding_limit) {
      return false;
    }
    session->slot_available.store(false);
    while (next_chunk < args.chunks_total &&
           session->in_flight.size() < args.outstanding_limit &&
           BenchNow() < point_deadline) {
      auto r = session->try_submit(next_chunk, peak_writes, peak_bytes);
      if (r == PointSession::SubmitOutcome::kOutstandingCap ||
          r == PointSession::SubmitOutcome::kResourceCap) {
        break;
      }
      if (r == PointSession::SubmitOutcome::kAllSubmitted) {
        break;
      }
      if (r == PointSession::SubmitOutcome::kWriteFail) {
        ++out.write_fail;
        ++out.write_attempts;
        break;
      }
      if (submit_begin.time_since_epoch().count() == 0) {
        submit_begin = session->records.back().submit;
      }
      submit_end = session->records.back().submit;
      ++next_chunk;
      ++out.write_attempts;
      any = true;
    }
    return any;
  };

  BenchInstant const point_start = BenchNow();
  BenchInstant last_progress_at{};
  if (args.progress_log && !args.progress_log_path.empty()) {
    std::ofstream pl(args.progress_log_path, std::ios::trunc);
    if (pl) {
      pl << "chunk_bytes=" << args.chunk_bytes
         << " outstanding=" << args.outstanding_limit << '\n';
    }
  }

  while (BenchNow() < point_deadline) {
    if (session->accept_submits.load()) {
      try_fill();
      if (next_chunk >= args.chunks_total) {
        session->accept_submits.store(false);
      }
    }
    bench.BenchPump(std::chrono::milliseconds{0});
    session->sync_record_terminals();
    session->prune_in_flight();
    UpdateEventPeaks(bench, out);
    MaybeLogProgress(bench, args, point_start, BenchNow(), last_progress_at, *session,
                     *session->receiver, next_chunk);

    if (PointFullyResolved(*session, *session->receiver)) {
      break;
    }
  }

  session->accept_submits.store(false);
  bench.BenchPump(std::chrono::milliseconds{0});
  session->sync_record_terminals();
  session->prune_in_flight();
  MaybeLogProgress(bench, args, point_start, BenchNow(), last_progress_at, *session,
                   *session->receiver, next_chunk);

  for (auto& flight : session->in_flight) {
    flight.status_sub.Reset();
  }
  session->in_flight.clear();

  if (args.progress_log && !args.chunk_timeline_path.empty()) {
    WriteChunkTimeline(args.chunk_timeline_path, point_start, *session,
                       *session->receiver);
  }

  {
    auto const u = bench.EventPoolUsageNow();
    out.events_registered_end = u.registered_events;
    out.handlers_active_end = u.handler_relationships;
  }

  out.timed_out = BenchNow() >= point_deadline && !session->receiver->Complete();

  for (auto const& r : session->records) {
    if (r.terminal_set) {
      if (r.status == kBenchStatusSuccess) {
        ++out.write_ok;
      } else if (r.status == kBenchStatusFail) {
        ++out.write_fail;
      } else if (r.status == kBenchStatusStop) {
        ++out.write_stop;
      } else if (r.status == kBenchStatusUnknown) {
        ++out.write_unknown;
        out.write_status_trusted = false;
      }
      if (!have_all_terminal) {
        all_terminal = r.terminal;
        have_all_terminal = true;
      } else if (r.terminal > all_terminal) {
        all_terminal = r.terminal;
      }
    } else {
      ++out.write_pending;
      out.write_status_trusted = false;
    }
  }

  out.generator_limit_events = session->generator_limit_events;
  out.sdk_capacity_events = session->sdk_capacity_events;
  out.peak_outstanding_writes = peak_writes;
  out.peak_outstanding_bytes = peak_bytes;
  out.dup_chunks = session->receiver->dup_chunks();
  out.rx_unique_bytes = session->receiver->bytes_unique();
  out.integrity_ok = !session->receiver->integrity_fail() &&
                     session->receiver->bytes_unique() == args.payload_total_bytes &&
                     session->receiver->Complete();
  out.delivery_ok = out.integrity_ok && out.write_unknown == 0 &&
                    out.write_pending == 0;

  out.submit_duration_s = DeltaMs(submit_begin, submit_end) / 1000.0;
  if (session->receiver->first_rx().time_since_epoch().count()) {
    out.first_rx_s = DeltaMs(submit_begin, session->receiver->first_rx()) / 1000.0;
    out.last_rx_s = DeltaMs(submit_begin, session->receiver->last_rx()) / 1000.0;
    if (out.delivery_ok && out.last_rx_s > 0.0) {
      out.goodput_mib_s =
          (static_cast<double>(args.payload_total_bytes) / (1024.0 * 1024.0)) /
          out.last_rx_s;
    }
  }
  if (have_all_terminal && out.write_pending == 0) {
    out.all_write_terminal_s = DeltaMs(submit_begin, all_terminal) / 1000.0;
  }

  std::vector<double> term_lat_ms;
  std::vector<double> rx_lat_ms;
  for (auto const& r : session->records) {
    if (r.terminal_set) {
      term_lat_ms.push_back(DeltaMs(r.submit, r.terminal));
    }
    auto rx = session->receiver->recv_time_for_chunk(r.chunk_index);
    if (rx.time_since_epoch().count() && r.submit.time_since_epoch().count()) {
      rx_lat_ms.push_back(DeltaMs(r.submit, rx));
    }
  }
  out.write_terminal_p50_ms = Percentile(term_lat_ms, 50);
  out.write_terminal_p95_ms = Percentile(term_lat_ms, 95);
  out.write_to_rx_p50_ms = Percentile(rx_lat_ms, 50);
  out.write_to_rx_p95_ms = Percentile(rx_lat_ms, 95);
  out.write_to_rx_max_ms =
      rx_lat_ms.empty() ? 0.0 : *std::max_element(rx_lat_ms.begin(), rx_lat_ms.end());

  if (out.delivery_ok) {
    out.outcome = "OK";
  } else if (out.timed_out) {
    out.outcome = "TIMEOUT";
  } else if (out.write_fail || out.write_stop) {
    out.outcome = "WRITE_ERRORS";
  } else if (out.write_unknown || out.write_pending) {
    out.outcome = "WRITE_UNRESOLVED";
  } else if (session->receiver->bytes_unique() < args.payload_total_bytes) {
    out.outcome = "INCOMPLETE";
  } else {
    out.outcome = "INTEGRITY_FAIL";
  }
  return out;
}

}  // namespace ae::stream_matrix_bench
