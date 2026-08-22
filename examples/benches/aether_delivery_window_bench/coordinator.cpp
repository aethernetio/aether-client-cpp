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

#include "coordinator.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "common/bench_buckets.h"
#include "common/bench_classify.h"
#include "common/bench_ipc.h"
#include "common/bench_stats.h"
#include "common/matrix.h"
#include "common/ring_trace.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace ae::bench::dw {
namespace {

struct ChildProc {
  Side side{Side::kA};
  NamedPipeServer pipe;
  std::unique_ptr<RingTrace<>> coord_side_trace{std::make_unique<RingTrace<>>()};
#if defined(_WIN32)
  PROCESS_INFORMATION pi{};
  bool running{false};
#endif
  std::uint64_t uid_lo{0};
  std::uint64_t uid_hi{0};
  bool ready{false};
};

struct CoordEvent {
  Side side{};
  EventKind kind{};
  std::uint32_t cycle_id{0};
  std::uint32_t message_id{0};
  std::uint16_t server_id{0};
  std::int64_t coord_us{0};
  std::int64_t local_us{0};
  std::int64_t a{0};
  std::int64_t b{0};
  std::int64_t c{0};
  Direction direction{Direction::kAtoB};
  std::uint32_t config_id{0};
};

std::int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string MakeRunId() {
  auto const t = std::chrono::system_clock::now().time_since_epoch();
  auto const ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(ms));
  return std::string{buf};
}

#if defined(_WIN32)
bool SpawnChild(ChildProc& child, std::string const& exe,
                std::string const& cmdline) {
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  ZeroMemory(&child.pi, sizeof(child.pi));
  std::string mutable_cmd = cmdline;
  auto ok = CreateProcessA(exe.c_str(), mutable_cmd.data(), nullptr, nullptr,
                           FALSE, 0, nullptr, nullptr, &si,
                           &child.pi);
  child.running = ok == TRUE;
  return child.running;
}

void StopChild(ChildProc& child, std::uint32_t graceful_ms) {
  if (!child.running) {
    return;
  }
  IpcFrame shutdown{};
  shutdown.type = static_cast<std::uint8_t>(IpcType::kShutdown);
  shutdown.side = static_cast<std::uint8_t>(Side::kCoordinator);
  child.pipe.WriteFrame(shutdown);

  auto const waited = WaitForSingleObject(child.pi.hProcess, graceful_ms);
  if (waited != WAIT_OBJECT_0) {
    TerminateProcess(child.pi.hProcess, 9);
    WaitForSingleObject(child.pi.hProcess, 5000);
  }
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
  child.pi = {};
  child.running = false;
  child.ready = false;
  child.pipe.Close();
}

bool PidAlive(ChildProc const& child) {
  if (!child.running) {
    return false;
  }
  auto const code = WaitForSingleObject(child.pi.hProcess, 0);
  return code == WAIT_TIMEOUT;
}
#endif

std::optional<CoordEvent> PollChild(ChildProc& child, std::uint32_t timeout_ms) {
  auto frame = child.pipe.TryReadFrame(timeout_ms);
  if (!frame) {
    return std::nullopt;
  }
  auto const coord_us = NowUs();
  CoordEvent ev;
  ev.side = child.side;
  ev.kind = static_cast<EventKind>(frame->event_kind);
  ev.cycle_id = frame->cycle_id;
  ev.message_id = frame->message_id;
  ev.server_id = frame->server_id;
  ev.coord_us = coord_us;
  ev.local_us = frame->local_steady_us;
  ev.a = frame->a;
  ev.b = frame->b;
  ev.c = frame->c;
  ev.direction = static_cast<Direction>(frame->direction);
  ev.config_id = frame->config_id;

  TraceEntry te;
  te.local_steady_us = coord_us;
  te.cycle_id = ev.cycle_id;
  te.message_id = ev.message_id;
  te.server_id = ev.server_id;
  te.event_kind = frame->event_kind;
  te.direction = frame->direction;
  te.a = frame->a;
  te.b = frame->b;
  te.c = frame->c;
  child.coord_side_trace->Push(te);

  auto const type = static_cast<IpcType>(frame->type);
  if (type == IpcType::kUidReport) {
    std::memcpy(&child.uid_lo, &frame->a, 8);
    std::memcpy(&child.uid_hi, &frame->b, 8);
  }
  if (type == IpcType::kChildReady || type == IpcType::kUidReport) {
    child.ready = true;
  }
  if (type == IpcType::kCalibPong) {
    ev.kind = EventKind::kAck;
  }
  return ev;
}

bool SendCmd(ChildProc& child, IpcType type, std::uint32_t config_id = 0,
             std::uint32_t cycle_id = 0, std::uint32_t message_id = 0,
             Direction dir = Direction::kAtoB, std::int64_t a = 0,
             std::int64_t b = 0, std::int64_t c = 0) {
  IpcFrame f{};
  f.type = static_cast<std::uint8_t>(type);
  f.side = static_cast<std::uint8_t>(Side::kCoordinator);
  f.config_id = config_id;
  f.cycle_id = cycle_id;
  f.message_id = message_id;
  f.direction = static_cast<std::uint8_t>(dir);
  f.a = a;
  f.b = b;
  f.c = c;
  f.local_steady_us = NowUs();
  return child.pipe.WriteFrame(f);
}

struct IpcCalibStats {
  double median_us{0};
  double p95_us{0};
  double max_us{0};
};

IpcCalibStats CalibrateIpc(ChildProc& child, std::string const& path) {
  std::vector<double> samples;
  samples.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    auto const t0 = NowUs();
    SendCmd(child, IpcType::kCalibPing);
    bool got = false;
    auto const deadline = NowUs() + 2'000'000;
    while (NowUs() < deadline) {
      auto ev = PollChild(child, 10);
      if (!ev) {
        continue;
      }
      // calib pong arrives as frame type calib_pong; accept any quick reply
      auto const t1 = NowUs();
      samples.push_back(static_cast<double>(t1 - t0));
      got = true;
      break;
    }
    if (!got) {
      break;
    }
  }
  auto summary = Summarize(samples);
  IpcCalibStats out;
  out.median_us = summary.p50.value_or(0);
  out.p95_us = summary.p95.value_or(0);
  out.max_us = summary.max.value_or(0);

  std::ofstream csv(path, std::ios::out | std::ios::trunc);
  csv << "rtt_us\n";
  for (auto v : samples) {
    csv << v << '\n';
  }
  csv << "median_us," << out.median_us << '\n';
  csv << "p95_us," << out.p95_us << '\n';
  csv << "max_us," << out.max_us << '\n';
  return out;
}

struct HypVerdict {
  std::string id;
  std::string verdict;
  std::string note;
};

std::vector<HypVerdict> BuildHypotheses(
    std::vector<SampleRecord> const& samples) {
  std::vector<HypVerdict> out;
  auto count_class = [&](Classification c) {
    int n = 0;
    for (auto const& s : samples) {
      if (s.valid && s.classification == c) {
        ++n;
      }
    }
    return n;
  };
  int valid = 0;
  int inside = 0;
  std::vector<double> inside_lat;
  for (auto const& s : samples) {
    if (!s.valid) {
      continue;
    }
    ++valid;
    if (s.target_bucket == Bucket::kInsideEarly ||
        s.target_bucket == Bucket::kInsideLate) {
      ++inside;
      if (s.accept_to_receive_ms > 0) {
        inside_lat.push_back(s.accept_to_receive_ms);
      }
    }
  }
  auto push_pct = count_class(Classification::kPushInsideWindow);
  auto outside_pct = count_class(Classification::kPushOutsideWindow);
  auto next_pct = count_class(Classification::kDeliveredAtNextWindow);
  auto pulled = count_class(Classification::kPulledExplicitly);

  {
    HypVerdict h;
    h.id = "H1";
    if (inside_lat.empty()) {
      h.verdict = "INCONCLUSIVE";
      h.note = "no inside-window latency samples";
    } else {
      auto s = Summarize(inside_lat);
      h.verdict = (s.p50 && *s.p50 < 500.0) ? "PASS" : "FAIL";
      h.note = "inside accept→receive p50=" +
               std::to_string(s.p50.value_or(-1));
    }
    out.push_back(h);
  }
  {
    HypVerdict h;
    h.id = "H2";
    h.verdict = (next_pct > outside_pct && next_pct > 0) ? "PASS" : "FAIL";
    h.note = "DELIVERED_AT_NEXT_WINDOW=" + std::to_string(next_pct) +
             " PUSH_OUTSIDE=" + std::to_string(outside_pct);
    out.push_back(h);
  }
  {
    HypVerdict h;
    h.id = "H3";
    h.verdict = outside_pct > 0 ? "PASS" : "FAIL";
    h.note = "PUSH_OUTSIDE_WINDOW=" + std::to_string(outside_pct);
    out.push_back(h);
  }
  {
    HypVerdict h;
    h.id = "H4";
    h.verdict = "INCONCLUSIVE";
    h.note = "compare Q2/Q4/Q5 push fractions in summary.csv";
    out.push_back(h);
  }
  {
    HypVerdict h;
    h.id = "H5";
    h.verdict = pulled > 0 ? "PASS" : "FAIL";
    h.note = "PULLED_EXPLICITLY=" + std::to_string(pulled);
    out.push_back(h);
  }
  (void)valid;
  (void)inside;
  (void)push_pct;
  return out;
}

bool StartPair(ChildProc& a, ChildProc& b, CoordinatorArgs const& args,
               MatrixConfig const& cfg, std::filesystem::path const& art,
               std::string const& name_a, std::string const& name_b) {
#if !defined(_WIN32)
  (void)a;
  (void)b;
  (void)args;
  (void)cfg;
  (void)art;
  (void)name_a;
  (void)name_b;
  return false;
#else
  a.side = Side::kA;
  b.side = Side::kB;
  auto pipe_a = PipeNameFor(args.run_id, Side::kA, cfg.id);
  auto pipe_b = PipeNameFor(args.run_id, Side::kB, cfg.id);
  if (!a.pipe.Create(pipe_a) || !b.pipe.Create(pipe_b)) {
    return false;
  }

  auto state_a = (std::filesystem::path{".artifacts"} /
                  "aether-delivery-window-bench" / "persistent-state" /
                  "state-a")
                     .string();
  auto state_b = (std::filesystem::path{".artifacts"} /
                  "aether-delivery-window-bench" / "persistent-state" /
                  "state-b")
                     .string();
  auto trace_a = (art / "client-a-trace.csv").string();
  auto trace_b = (art / "client-b-trace.csv").string();

  auto make_cmd = [&](char side, std::string const& pipe,
                      std::string const& state, std::string const& name,
                      std::string const& trace) {
    std::ostringstream oss;
    oss << '"' << args.exe_path << '"' << " --role client --side " << side
        << " --run-id " << args.run_id << " --pipe " << pipe << " --state-dir "
        << '"' << state << '"' << " --client-name " << name << " --trace "
        << '"' << trace << '"' << " --parent-uid " << args.parent_uid
        << " --actual-ms " << cfg.timing.actual_ping_interval_ms
        << " --announced-ms " << cfg.timing.announced_next_ping_ms
        << " --rx-window-ms " << cfg.timing.rx_window_ms;
    return oss.str();
  };

  if (!SpawnChild(a, args.exe_path,
                  make_cmd('A', pipe_a, state_a, name_a, trace_a))) {
    return false;
  }
  if (!SpawnChild(b, args.exe_path,
                  make_cmd('B', pipe_b, state_b, name_b, trace_b))) {
    StopChild(a, 1000);
    return false;
  }
  if (!a.pipe.WaitForClient(60000) || !b.pipe.WaitForClient(60000)) {
    StopChild(a, 1000);
    StopChild(b, 1000);
    return false;
  }

  auto wait_ready = [&](ChildProc& c) {
    // Fresh registration can take >5 min (WaitKeys timeout + PoW).
    auto const deadline = NowUs() + 900'000'000;  // 15 min
    while (NowUs() < deadline && !c.ready) {
      PollChild(c, 100);
    }
    return c.ready;
  };
  if (!wait_ready(a) || !wait_ready(b)) {
    StopChild(a, 2000);
    StopChild(b, 2000);
    return false;
  }
  return true;
#endif
}

void ExchangePeerUids(ChildProc& a, ChildProc& b) {
  std::int64_t a_lo = 0;
  std::int64_t a_hi = 0;
  std::int64_t b_lo = 0;
  std::int64_t b_hi = 0;
  std::memcpy(&a_lo, &a.uid_lo, 8);
  std::memcpy(&a_hi, &a.uid_hi, 8);
  std::memcpy(&b_lo, &b.uid_lo, 8);
  std::memcpy(&b_hi, &b.uid_hi, 8);
  SendCmd(a, IpcType::kSetPeerUid, 0, 0, 0, Direction::kAtoB, b_lo, b_hi);
  SendCmd(b, IpcType::kSetPeerUid, 0, 0, 0, Direction::kBtoA, a_lo, a_hi);
  for (int i = 0; i < 40; ++i) {
    PollChild(a, 20);
    PollChild(b, 20);
  }
}

bool WaitWarmup(ChildProc& child, int opens) {
  SendCmd(child, IpcType::kWaitWarmupPings, 0, 0, 0, Direction::kAtoB, opens);
  int seen_opens = 0;
  auto const end =
      NowUs() + (opens + 3) * 30'000'000LL;
  while (NowUs() < end) {
    auto ev = PollChild(child, 100);
    if (!ev) {
      continue;
    }
    if (ev->kind == EventKind::kRxWindowOpen) {
      ++seen_opens;
    }
    if (ev->kind == EventKind::kWarmupDone || seen_opens >= opens) {
      return true;
    }
  }
  return seen_opens >= opens;
}

std::optional<SampleRecord> RunOneSample(
    ChildProc& sender, ChildProc& receiver, MatrixConfig const& cfg,
    Bucket bucket, Direction dir, std::uint32_t cycle_id,
    std::uint32_t message_id, std::string const& run_id, int sample_id,
    bool pull_control) {
  SampleRecord rec;
  rec.run_id = run_id;
  rec.direction = dir;
  rec.config_id = cfg.id;
  rec.sample_id = sample_id;
  rec.cycle_id = cycle_id;
  rec.timing = cfg.timing;
  rec.target_bucket = bucket;
  rec.pull_control = pull_control;

  auto offset = OffsetForBucket(bucket, cfg.timing.actual_ping_interval_ms,
                                cfg.timing.rx_window_ms);
  if (!offset) {
    rec.invalid_reason = "bucket_unavailable";
    return rec;
  }

  // Wait RX_WINDOW_OPEN on receiver.
  std::uint16_t window_server = 0;
  auto const open_deadline =
      NowUs() + (cfg.timing.actual_ping_interval_ms + 5000) * 1000;
  while (NowUs() < open_deadline) {
    auto ev = PollChild(receiver, 50);
    PollChild(sender, 1);
    if (!ev) {
      continue;
    }
    if (ev->kind == EventKind::kRxWindowOpen) {
      rec.window_open_us = ev->coord_us;
      window_server = ev->server_id;
      break;
    }
  }
  if (rec.window_open_us == 0) {
    rec.invalid_reason = "no_window_open";
    return rec;
  }

  if (pull_control) {
    // wait close then outside early offset from close conceptually handled by
    // offset relative to open (outside early).
    auto const close_deadline =
        NowUs() + (cfg.timing.rx_window_ms + 2000) * 1000;
    while (NowUs() < close_deadline) {
      auto ev = PollChild(receiver, 20);
      PollChild(sender, 1);
      if (ev && ev->kind == EventKind::kRxWindowClose &&
          ev->server_id == window_server) {
        rec.window_close_us = ev->coord_us;
        break;
      }
    }
  }

#if defined(_WIN32)
  {
    auto const elapsed_ms =
        (NowUs() - rec.window_open_us) / 1000;
    auto const remain_ms = *offset - elapsed_ms;
    if (remain_ms < -50) {
      rec.invalid_reason = "send_offset_missed";
      return rec;
    }
    if (remain_ms > 0) {
      WaitMsPrecise(remain_ms);
    }
  }
#endif
  rec.send_command_us = NowUs();
  auto config_hash = ConfigIdHash(cfg.id);
  SendCmd(sender, IpcType::kSendMessage, config_hash, cycle_id, message_id, dir);

  auto const accept_deadline =
      NowUs() + (cfg.timing.actual_ping_interval_ms + 60000) * 1000;
  while (NowUs() < accept_deadline && rec.server_accept_us == 0) {
    auto ev_s = PollChild(sender, 20);
    auto ev_r = PollChild(receiver, 20);
    if (ev_s) {
      if (ev_s->kind == EventKind::kMessageSendCall &&
          ev_s->message_id == message_id) {
        rec.send_call_us = ev_s->coord_us;
      }
      if (ev_s->kind == EventKind::kMessageServerAccepted &&
          ev_s->message_id == message_id) {
        rec.server_accept_us = ev_s->coord_us;
        rec.server_id = ev_s->server_id;
      }
      if (ev_s->kind == EventKind::kError &&
          ev_s->message_id == message_id) {
        rec.invalid_reason =
            "send_error_" + std::to_string(ev_s->a);
        return rec;
      }
    }
    if (ev_r && ev_r->kind == EventKind::kMessageReceived &&
        ev_r->message_id == message_id) {
      rec.receive_us = ev_r->coord_us;
      rec.duplicate_count = static_cast<int>(ev_r->a);
    }
    if (ev_r && ev_r->kind == EventKind::kRxWindowClose &&
        ev_r->server_id == window_server && rec.window_close_us == 0) {
      rec.window_close_us = ev_r->coord_us;
    }
  }

  if (rec.server_accept_us == 0) {
    rec.invalid_reason = rec.send_call_us == 0 ? "no_send_call" : "no_server_accept";
    return rec;
  }
  if (!WindowsMatchServer(rec.server_id, window_server)) {
    rec.invalid_reason = "server_id_mismatch";
    return rec;
  }

  rec.actual_accept_bucket = ClassifyAcceptBucket(
      rec.server_accept_us, rec.window_open_us, rec.window_close_us,
      cfg.timing.actual_ping_interval_ms, cfg.timing.rx_window_ms, bucket);
  if (!AcceptMatchesTargetBucket(rec.actual_accept_bucket, bucket) &&
      !pull_control) {
    rec.invalid_reason = "INVALID_BOUNDARY";
    return rec;
  }

  if (pull_control && rec.receive_us == 0) {
#if defined(_WIN32)
    WaitMsPrecise(500);
#endif
    // check receive after wait
    for (int i = 0; i < 50; ++i) {
      auto ev_r = PollChild(receiver, 10);
      if (ev_r && ev_r->kind == EventKind::kMessageReceived &&
          ev_r->message_id == message_id) {
        rec.receive_us = ev_r->coord_us;
        rec.duplicate_count = static_cast<int>(ev_r->a);
        rec.classification = Classification::kPushOutsideWindow;
        break;
      }
    }
    if (rec.receive_us == 0) {
      rec.pull_request_us = NowUs();
      SendCmd(receiver, IpcType::kPullMessages, config_hash, cycle_id,
              message_id);
    }
  }

  auto const recv_deadline =
      NowUs() +
      (2 * cfg.timing.actual_ping_interval_ms + 5000) * 1000;
  while (NowUs() < recv_deadline) {
    auto ev_r = PollChild(receiver, 20);
    PollChild(sender, 1);
    if (!ev_r) {
      continue;
    }
    if (ev_r->kind == EventKind::kMessageReceived &&
        ev_r->message_id == message_id) {
      if (rec.receive_us == 0) {
        rec.receive_us = ev_r->coord_us;
      }
      rec.duplicate_count = static_cast<int>(ev_r->a) > 1
                                ? static_cast<int>(ev_r->a) - 1
                                : 0;
    }
    if (ev_r->kind == EventKind::kRxWindowOpen &&
        ev_r->server_id == window_server &&
        ev_r->coord_us > rec.window_open_us && rec.next_window_open_us == 0) {
      rec.next_window_open_us = ev_r->coord_us;
    }
    if (ev_r->kind == EventKind::kRxWindowClose &&
        ev_r->server_id == window_server && rec.window_close_us == 0) {
      rec.window_close_us = ev_r->coord_us;
    }
    if (rec.receive_us > 0 && rec.next_window_open_us > 0) {
      break;
    }
  }

  if (rec.send_call_us > 0 && rec.server_accept_us > 0) {
    rec.send_to_accept_ms =
        (rec.server_accept_us - rec.send_call_us) / 1000.0;
  }
  if (rec.server_accept_us > 0 && rec.receive_us > 0) {
    rec.accept_to_receive_ms =
        (rec.receive_us - rec.server_accept_us) / 1000.0;
  }
  if (rec.send_call_us > 0 && rec.receive_us > 0) {
    rec.send_to_receive_ms = (rec.receive_us - rec.send_call_us) / 1000.0;
  }

  ClassifyInput cin;
  cin.window_open_us = rec.window_open_us;
  cin.window_close_us = rec.window_close_us;
  cin.server_accept_us = rec.server_accept_us;
  cin.receive_us = rec.receive_us;
  cin.next_window_open_us = rec.next_window_open_us;
  cin.pull_request_us = rec.pull_request_us;
  cin.actual_interval_ms = cfg.timing.actual_ping_interval_ms;
  cin.duplicate_count = rec.duplicate_count;
  cin.pull_control = pull_control && rec.pull_request_us > 0;
  rec.classification = ClassifySample(cin);
  rec.valid = rec.invalid_reason.empty() &&
              rec.classification != Classification::kInvalid;
  return rec;
}

void WriteSamplesCsv(std::filesystem::path const& path,
                     std::vector<SampleRecord> const& samples) {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << "run_id,direction,config_id,sample_id,cycle_id,server_id,"
         "actual_interval_ms,announced_next_ms,rx_window_ms,target_bucket,"
         "actual_accept_bucket,window_open_us,window_close_us,send_command_us,"
         "send_call_us,server_accept_us,receive_us,next_window_open_us,"
         "send_to_accept_ms,accept_to_receive_ms,send_to_receive_ms,"
         "classification,duplicate_count,valid,invalid_reason\n";
  for (auto const& s : samples) {
    out << s.run_id << ',' << DirectionName(s.direction) << ',' << s.config_id
        << ',' << s.sample_id << ',' << s.cycle_id << ',' << s.server_id << ','
        << s.timing.actual_ping_interval_ms << ','
        << s.timing.announced_next_ping_ms << ',' << s.timing.rx_window_ms
        << ',' << BucketName(s.target_bucket) << ','
        << BucketName(s.actual_accept_bucket) << ',' << s.window_open_us << ','
        << s.window_close_us << ',' << s.send_command_us << ','
        << s.send_call_us << ',' << s.server_accept_us << ',' << s.receive_us
        << ',' << s.next_window_open_us << ',' << s.send_to_accept_ms << ','
        << s.accept_to_receive_ms << ',' << s.send_to_receive_ms << ','
        << ClassificationName(s.classification) << ',' << s.duplicate_count
        << ',' << (s.valid ? 1 : 0) << ',' << s.invalid_reason << '\n';
  }
}

void WriteSummary(std::filesystem::path const& csv_path,
                  std::filesystem::path const& json_path,
                  std::vector<SampleRecord> const& samples,
                  std::vector<HypVerdict> const& hyps,
                  std::string const& run_id,
                  std::filesystem::path const& art) {
  struct Key {
    std::string direction;
    std::string config;
    std::string bucket;
    bool operator<(Key const& o) const {
      if (direction != o.direction) return direction < o.direction;
      if (config != o.config) return config < o.config;
      return bucket < o.bucket;
    }
  };
  std::map<Key, std::vector<SampleRecord const*>> groups;
  for (auto const& s : samples) {
    Key k{std::string{DirectionName(s.direction)}, s.config_id,
          std::string{BucketName(s.target_bucket)}};
    groups[k].push_back(&s);
  }

  std::ofstream csv(csv_path, std::ios::out | std::ios::trunc);
  csv << "direction,config,bucket,valid_samples,invalid_samples,lost,"
         "duplicates,send_to_accept_p50,send_to_accept_p95,"
         "accept_to_receive_p50,accept_to_receive_p95,accept_to_receive_max,"
         "push_inside_percent,push_outside_percent,next_window_percent,"
         "pulled_percent\n";

  std::cout << "\nconfig | dir | bucket | n | accept->recv p50/p95/max | "
               "classes\n";

  for (auto const& [k, vec] : groups) {
    int valid = 0, invalid = 0, lost = 0, dups = 0;
    int push_in = 0, push_out = 0, next_w = 0, pulled = 0;
    std::vector<double> s2a, a2r;
    for (auto const* s : vec) {
      if (s->valid) {
        ++valid;
        if (s->send_to_accept_ms > 0) s2a.push_back(s->send_to_accept_ms);
        if (s->accept_to_receive_ms > 0) a2r.push_back(s->accept_to_receive_ms);
        switch (s->classification) {
          case Classification::kPushInsideWindow:
            ++push_in;
            break;
          case Classification::kPushOutsideWindow:
            ++push_out;
            break;
          case Classification::kDeliveredAtNextWindow:
            ++next_w;
            break;
          case Classification::kPulledExplicitly:
            ++pulled;
            break;
          case Classification::kLost:
            ++lost;
            break;
          case Classification::kDuplicate:
            ++dups;
            break;
          default:
            break;
        }
      } else {
        ++invalid;
      }
    }
    auto qs2a = Summarize(s2a);
    auto qa2r = Summarize(a2r);
    double denom = valid > 0 ? static_cast<double>(valid) : 1.0;
    csv << k.direction << ',' << k.config << ',' << k.bucket << ',' << valid
        << ',' << invalid << ',' << lost << ',' << dups << ','
        << qs2a.p50.value_or(-1) << ',' << qs2a.p95.value_or(-1) << ','
        << qa2r.p50.value_or(-1) << ',' << qa2r.p95.value_or(-1) << ','
        << qa2r.max.value_or(-1) << ',' << (100.0 * push_in / denom) << ','
        << (100.0 * push_out / denom) << ',' << (100.0 * next_w / denom) << ','
        << (100.0 * pulled / denom) << '\n';

    std::cout << k.config << " | " << k.direction << " | " << k.bucket << " | "
              << valid << " | " << qa2r.p50.value_or(-1) << "/"
              << qa2r.p95.value_or(-1) << "/" << qa2r.max.value_or(-1)
              << " | in=" << push_in << " out=" << push_out
              << " next=" << next_w << " pull=" << pulled << "\n";
  }

  std::ofstream json(json_path, std::ios::out | std::ios::trunc);
  json << "{\n  \"run_id\": \"" << run_id << "\",\n  \"artifact_dir\": \""
       << art.string() << "\",\n  \"hypotheses\": [\n";
  for (std::size_t i = 0; i < hyps.size(); ++i) {
    json << "    {\"id\": \"" << hyps[i].id << "\", \"verdict\": \""
         << hyps[i].verdict << "\", \"note\": \"" << hyps[i].note << "\"}";
    if (i + 1 < hyps.size()) json << ',';
    json << '\n';
  }
  json << "  ]\n}\n";
}

}  // namespace

int RunCoordinator(CoordinatorArgs const& args_in) {
#if !defined(_WIN32)
  std::cout << "Windows-only benchmark\n";
  return 2;
#else
  CoordinatorArgs args = args_in;
  if (args.run_id.empty()) {
    args.run_id = MakeRunId();
  }
  if (args.artifact_dir.empty()) {
    args.artifact_dir =
        (std::filesystem::path{".artifacts"} / "aether-delivery-window-bench" /
         args.run_id)
            .string();
  }
  if (args.exe_path.empty()) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    args.exe_path = path;
  }

  auto art = std::filesystem::path{args.artifact_dir};
  std::filesystem::create_directories(art);
  // Persist UIDs across runs under a stable path (not wiped per run_id).
  auto state_root =
      std::filesystem::path{".artifacts"} / "aether-delivery-window-bench" /
      "persistent-state";
  std::filesystem::create_directories(state_root / "state-a");
  std::filesystem::create_directories(state_root / "state-b");
  // Also ensure per-run dirs exist for traces.
  std::filesystem::create_directories(art / "state-a");
  std::filesystem::create_directories(art / "state-b");
  (void)state_root;

  std::cout << "build: Release\n" << std::flush;
  std::cout << "run_id: " << args.run_id << "\n" << std::flush;
  std::cout << "artifact_dir: " << art.string() << "\n" << std::flush;
  std::cout << "exe: " << args.exe_path << "\n" << std::flush;

  auto configs = (args.matrix == "smoke") ? SmokeMatrix() : QuickMatrix();
  std::vector<SampleRecord> all_samples;
  IpcCalibStats calib{};

  std::uint32_t cycle_id = 1;
  std::uint32_t message_id = 1;

  for (auto const& cfg : configs) {
    auto child_a = std::make_unique<ChildProc>();
    auto child_b = std::make_unique<ChildProc>();
    if (!StartPair(*child_a, *child_b, args, cfg, art, "dw-bench-a",
                   "dw-bench-b")) {
      std::cout << "failed to start children for " << cfg.id << "\n"
                << std::flush;
      return 3;
    }
    std::cout << "children ready for " << cfg.id << "\n" << std::flush;

    if (calib.max_us == 0) {
      calib = CalibrateIpc(*child_a, (art / "ipc_calibration.csv").string());
    }

    ExchangePeerUids(*child_a, *child_b);
    // Allow peer-cloud resolve before first send.
    {
      auto const deadline = NowUs() + 60'000'000;
      while (NowUs() < deadline) {
        PollChild(*child_a, 50);
        PollChild(*child_b, 50);
      }
    }
    WaitWarmup(*child_a, 2);
    WaitWarmup(*child_b, 2);

    SendCmd(*child_a, IpcType::kUapVerify, ConfigIdHash(cfg.id));
    SendCmd(*child_b, IpcType::kUapVerify, ConfigIdHash(cfg.id));
    auto uap_deadline = NowUs() + 30'000'000;
    int uap_ok = 0;
    while (NowUs() < uap_deadline && uap_ok < 2) {
      for (auto* c : {child_a.get(), child_b.get()}) {
        auto ev = PollChild(*c, 50);
        if (ev && static_cast<EventKind>(ev->kind) == EventKind::kAck) {
          // UapResult frames use type UapResult; approximate via events.
        }
        if (ev && ev->kind == EventKind::kUapDelta) {
          ++uap_ok;
        }
      }
    }

    // online warm-up message A→B
    {
      auto warm = RunOneSample(*child_a, *child_b, cfg, Bucket::kInsideEarly,
                               Direction::kAtoB, cycle_id++, message_id++,
                               args.run_id, -1, false);
      (void)warm;
    }

    for (auto dir : {Direction::kAtoB, Direction::kBtoA}) {
      ChildProc* sender = dir == Direction::kAtoB ? child_a.get() : child_b.get();
      ChildProc* receiver =
          dir == Direction::kAtoB ? child_b.get() : child_a.get();
      for (auto bucket : cfg.buckets) {
        int got = 0;
        int attempts = 0;
        while (got < cfg.samples_per_bucket &&
               attempts < cfg.samples_per_bucket * 4) {
          ++attempts;
          auto sample = RunOneSample(*sender, *receiver, cfg, bucket, dir,
                                     cycle_id++, message_id++, args.run_id, got,
                                     false);
          if (sample && sample->valid) {
            all_samples.push_back(*sample);
            ++got;
            std::cout << "sample OK " << cfg.id << " "
                      << DirectionName(dir) << " "
                      << BucketName(bucket) << " #" << got << " class="
                      << ClassificationName(sample->classification)
                      << " a2r_ms=" << sample->accept_to_receive_ms << "\n"
                      << std::flush;
          } else if (sample) {
            all_samples.push_back(*sample);
            std::cout << "sample INVALID " << cfg.id << " "
                      << sample->invalid_reason << "\n"
                      << std::flush;
          }
        }
      }
    }

    // PART M pull control for Q2
    if (cfg.id == "Q2") {
      int pulls = 0;
      int attempts = 0;
      while (pulls < 1 && attempts < 10) {
        ++attempts;
        auto sample =
            RunOneSample(*child_a, *child_b, cfg, Bucket::kOutsideEarly,
                         Direction::kAtoB, cycle_id++, message_id++,
                         args.run_id, 900 + attempts, true);
        if (sample) {
          all_samples.push_back(*sample);
          if (sample->valid &&
              (sample->classification == Classification::kPulledExplicitly ||
               sample->classification == Classification::kPushOutsideWindow)) {
            ++pulls;
          }
        }
      }
    }

    SendCmd(*child_a, IpcType::kFlushTrace);
    SendCmd(*child_b, IpcType::kFlushTrace);
    PollChild(*child_a, 200);
    PollChild(*child_b, 200);
    StopChild(*child_a, 10000);
    StopChild(*child_b, 10000);
    if (PidAlive(*child_a) || PidAlive(*child_b)) {
      std::cout << "child leak after " << cfg.id << "\n";
    }
  }

  auto coordinator_trace = std::make_unique<RingTrace<>>();
  WriteSamplesCsv(art / "samples.csv", all_samples);
  auto hyps = BuildHypotheses(all_samples);
  WriteSummary(art / "summary.csv", art / "summary.json", all_samples, hyps,
               args.run_id, art);
  coordinator_trace->FlushCsv((art / "coordinator-trace.csv").string());

  std::cout << "artifact_dir: " << art.string() << "\n";
  std::cout << "ipc_calib_us median/p95/max: " << calib.median_us << "/"
            << calib.p95_us << "/" << calib.max_us << "\n";
  for (auto const& h : hyps) {
    std::cout << h.id << " " << h.verdict << " — " << h.note << "\n";
  }
  return 0;
#endif
}

}  // namespace ae::bench::dw
