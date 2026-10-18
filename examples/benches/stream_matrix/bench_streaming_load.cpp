/*
 * Copyright 2026 Aethernet Inc.
 */
#include "stream_matrix/bench_streaming_load.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>

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

struct ActiveWrite {
  std::uint64_t chunk_index{};
  std::size_t bytes{};
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
  std::atomic<bool> alive{true};
  std::atomic<bool> slot_available{true};
  std::uint64_t generator_limit_events{0};
  std::uint64_t sdk_capacity_events{0};
  std::uint64_t write_fail_events{0};
  std::vector<ChunkWriteRecord> records;
  std::vector<ChunkTerminalSlot> terminals;
  std::deque<ActiveWrite> active;

  void note_peaks(std::uint64_t& peak_writes, std::uint64_t& peak_bytes) {
    peak_writes = std::max(peak_writes, active.size());
    std::uint64_t ob = 0;
    for (auto const& a : active) {
      ob += a.bytes;
    }
    peak_bytes = std::max(peak_bytes, ob);
  }

  void mark_terminal(std::uint64_t chunk_index, WriteAction::Status st) {
    if (chunk_index >= terminals.size()) {
      return;
    }
    auto& slot = terminals[chunk_index];
    if (slot.terminal_set) {
      return;
    }
    slot.terminal = BenchNow();
    slot.terminal_set = true;
    if (st == WriteAction::Status::kSuccess) {
      slot.status = 0;
    } else if (st == WriteAction::Status::kFail) {
      slot.status = 1;
      ++write_fail_events;
    } else {
      slot.status = 2;
    }
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

  enum class SubmitOutcome { kSubmitted, kOutstandingCap, kAllSubmitted, kWriteFail };

  SubmitOutcome try_submit(std::uint64_t chunk_index, std::uint64_t& peak_writes,
                           std::uint64_t& peak_bytes) {
    if (!alive.load()) {
      return SubmitOutcome::kAllSubmitted;
    }
    if (chunk_index >= chunks_total) {
      return SubmitOutcome::kAllSubmitted;
    }
    if (active.size() >= outstanding_limit) {
      return SubmitOutcome::kOutstandingCap;
    }
    auto const byte_offset = chunk_index * chunk_bytes;
    auto const remaining = kThroughputTotalBytes - byte_offset;
    auto const payload_len = std::min(chunk_bytes, remaining);
    ChunkWriteRecord rec{
        .chunk_index = chunk_index,
        .payload_bytes = payload_len,
        .submit = BenchNow(),
    };
    records.push_back(rec);
    auto wire = MakeThptChunk(*blob, run_id, chunk_index, byte_offset, payload_len);
    auto& wa = send_stream->Write(std::move(wire));
    std::weak_ptr<PointSession> weak = shared_from_this();
    bench->StreamingPointWriteSubs() += wa.status_event().Subscribe(
        [weak, chunk_index](WriteAction::Status st) {
          auto self = weak.lock();
          if (!self || !self->alive.load()) {
            return;
          }
          self->mark_terminal(chunk_index, st);
          self->slot_available.store(true);
        });
    active.push_back(ActiveWrite{.chunk_index = chunk_index, .bytes = payload_len});
    note_peaks(peak_writes, peak_bytes);
    if (wa.is_finished()) {
      WriteAction::Status st = WriteAction::Status::kSuccess;
      mark_terminal(chunk_index, st);
      slot_available.store(true);
    }
    return SubmitOutcome::kSubmitted;
  }

  void drain_finished() {
    for (auto it = active.begin(); it != active.end();) {
      if (it->chunk_index >= terminals.size() ||
          !terminals[it->chunk_index].terminal_set) {
        ++it;
        continue;
      }
      it = active.erase(it);
    }
  }
};

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
  session->records.reserve(args.chunks_total);
  session->terminals.assign(args.chunks_total, ChunkTerminalSlot{});

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
        if (session->active.size() >= args.outstanding_limit) {
          break;
        }
        auto r = session->try_submit(next_chunk, peak_writes, peak_bytes);
        if (r == PointSession::SubmitOutcome::kOutstandingCap ||
            r == PointSession::SubmitOutcome::kAllSubmitted) {
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
        session->drain_finished();
      }
      return any;
    }
    if (!session->slot_available.load() &&
        session->active.size() >= args.outstanding_limit) {
      return false;
    }
    session->slot_available.store(false);
    while (next_chunk < args.chunks_total &&
           session->active.size() < args.outstanding_limit &&
           BenchNow() < point_deadline) {
      auto r = session->try_submit(next_chunk, peak_writes, peak_bytes);
      if (r == PointSession::SubmitOutcome::kOutstandingCap) {
        break;
      }
      if (r == PointSession::SubmitOutcome::kAllSubmitted) {
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

  while (BenchNow() < point_deadline && session->alive.load()) {
    try_fill();
    bench.BenchPump(std::chrono::milliseconds{0});
    session->sync_record_terminals();
    session->drain_finished();

    if (next_chunk >= args.chunks_total && session->active.empty() &&
        session->receiver->Complete()) {
      break;
    }
  }

  session->alive.store(false);
  BenchInstant const drain_until = BenchNow() + std::chrono::seconds{2};
  while (BenchNow() < drain_until && !session->active.empty()) {
    bench.BenchPump(std::chrono::milliseconds{0});
    session->sync_record_terminals();
    session->drain_finished();
  }
  session->sync_record_terminals();
  bench.StreamingPointWriteSubs().Reset();

  out.timed_out =
      BenchNow() >= point_deadline && !session->receiver->Complete();

  for (auto const& r : session->records) {
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

  out.generator_limit_events = session->generator_limit_events;
  out.sdk_capacity_events = session->sdk_capacity_events;
  out.peak_outstanding_writes = peak_writes;
  out.peak_outstanding_bytes = peak_bytes;
  out.dup_chunks = session->receiver->dup_chunks();
  out.rx_unique_bytes = session->receiver->bytes_unique();
  out.integrity_ok = !session->receiver->integrity_fail() &&
                     session->receiver->bytes_unique() == kThroughputTotalBytes &&
                     session->receiver->Complete();
  out.delivery_ok = out.integrity_ok;

  out.submit_duration_s = DeltaMs(submit_begin, submit_end) / 1000.0;
  if (session->receiver->first_rx().time_since_epoch().count()) {
    out.first_rx_s = DeltaMs(submit_begin, session->receiver->first_rx()) / 1000.0;
    out.last_rx_s = DeltaMs(submit_begin, session->receiver->last_rx()) / 1000.0;
    if (out.delivery_ok && out.last_rx_s > 0.0) {
      out.goodput_mib_s =
          (static_cast<double>(kThroughputTotalBytes) / (1024.0 * 1024.0)) /
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
  } else if (session->receiver->bytes_unique() < kThroughputTotalBytes) {
    out.outcome = "INCOMPLETE";
  } else {
    out.outcome = "INTEGRITY_FAIL";
  }
  return out;
}

}  // namespace ae::stream_matrix_bench
