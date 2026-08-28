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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include "common/bench_ipc.h"
#include "common/bench_types.h"
#include "common/udp_proof_types.h"

#include "aether/config.h"

namespace ae::bench::uap {
namespace {

constexpr int kOffsetsMs[] = {500, 800, 1500, 2500};
#if defined(AE_UAP_DELIVERY_REQUIRE_UDP) && AE_UAP_DELIVERY_REQUIRE_UDP
constexpr int kSamplesPerOffset = 30;
constexpr int kMinValidPerOffset = 30;
#else
constexpr int kSamplesPerOffset = 20;
constexpr int kMinValidPerOffset = 20;
#endif

char const* SkipReasonString(std::int64_t code) {
  switch (code) {
    case 1:
      return "skipped_too_close";
    case 2:
      return "skipped_stale";
    case 3:
      return "skipped_delay_too_long";
    case 5:
      return "skipped_tcp_refuse";
    case 6:
      return "INVALID_ROUTE_CHANGED";
    case 7:
      return "no_dest_route";
    case 8:
      return "no_dest_server_timing";
    default:
      return "skipped_cycle";
  }
}

struct BobPingEvent {
  std::uint8_t kind{0};
  std::int64_t server_id{0};
  std::int64_t planned_us{0};
  std::int64_t actual_us{0};
  std::int64_t early_by_us{0};
  std::int64_t base_window_us{0};
  std::int64_t effective_window_us{0};
  std::int64_t required_until_us{0};
  std::int64_t next_planned_us{0};
  std::int64_t guard_us{0};
  std::int64_t min_rtt_us{0};
  std::int64_t p99_rtt_us{0};
  std::int64_t channel_generation{0};
  std::int64_t result_type{0};
  std::int64_t event_steady_us{0};
};

void AttachAndClassify(SampleRecord& rec,
                       std::vector<BobPingEvent> const& pings) {
  if (rec.duplicate_count > 1) {
    rec.classification = "DUPLICATE";
    return;
  }
  if (rec.invalid_reason == "INVALID_ROUTE_CHANGED" ||
      rec.invalid_reason == "ROUTE_MISMATCH") {
    rec.classification = "ROUTE_CHANGED";
    return;
  }

  BobPingEvent const* covering = nullptr;
  if (rec.receive_us > 0) {
    for (auto const& p : pings) {
      if (p.kind != 1) {
        continue;
      }
      if (p.server_id != rec.actual_send_server_id &&
          p.server_id != rec.schedule_server_id) {
        continue;
      }
      if (p.event_steady_us > rec.receive_us) {
        continue;
      }
      auto const end = p.event_steady_us + p.effective_window_us;
      if (rec.receive_us <= end) {
        covering = &p;
      }
    }
  }

  if (covering != nullptr) {
    rec.bob_ping_server_id = covering->server_id;
    rec.bob_ping_planned_send_us = covering->planned_us;
    rec.bob_ping_actual_send_us = covering->actual_us;
    rec.bob_ping_early_by_us = covering->early_by_us;
    rec.bob_base_rx_window_us = covering->base_window_us;
    rec.bob_effective_wire_rx_window_us = covering->effective_window_us;
    rec.bob_required_rx_until_us = covering->required_until_us;
    rec.bob_ping_guard_us = covering->guard_us;
    for (auto const& p : pings) {
      if (p.kind == 2 && p.server_id == covering->server_id &&
          p.actual_us == covering->actual_us) {
        rec.bob_ping_result_us = p.event_steady_us;
      }
    }
    bool const early = covering->early_by_us > 0 &&
                       covering->effective_window_us > covering->base_window_us;
    if (early) {
      rec.classification = "EARLY_PING_EXTENDED_WINDOW";
    } else if (rec.offset_ms >= 1000) {
      rec.classification = "WAITED_FOR_NEXT_WINDOW";
    } else {
      rec.classification = "CURRENT_NORMAL_WINDOW";
    }
    return;
  }

  if (rec.receive_us > 0) {
    rec.classification = "LATE_UNEXPLAINED";
    return;
  }
  rec.classification = "LOST";
}

struct ChildProc {
  Side side{};
  NamedPipeServer pipe;
  PROCESS_INFORMATION pi{};
  std::uint64_t uid_lo{0};
  std::uint64_t uid_hi{0};
  bool ready{false};
  bool uid_ok{false};
  std::uint32_t seq{0};
  ChannelProof own_proof{};
  ChannelProof dest_proof{};
  bool got_own_proof{false};
  bool got_dest_proof{false};
  std::vector<BobPingEvent> ping_events;
};

std::string MakeRunId() {
  SYSTEMTIME st{};
  GetSystemTime(&st);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u", st.wYear,
                st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return buf;
}

std::string DefaultExePath() {
  char path[MAX_PATH]{};
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  return path;
}

bool SendCmd(ChildProc& child, IpcType type, std::uint32_t sequence = 0,
             std::uint32_t offset_ms = 0, std::int64_t a = 0,
             std::int64_t b = 0, std::int64_t c = 0) {
  IpcFrame f{};
  f.type = static_cast<std::uint8_t>(type);
  f.side = static_cast<std::uint8_t>(Side::kCoordinator);
  f.seq = ++child.seq;
  f.sequence = sequence;
  f.offset_ms = offset_ms;
  f.a = a;
  f.b = b;
  f.c = c;
  return child.pipe.WriteFrame(f);
}

void HandleChildFrame(ChildProc& child, IpcFrame const& frame) {
  auto const type = static_cast<IpcType>(frame.type);
  if (type == IpcType::kUidReport) {
    std::memcpy(&child.uid_lo, &frame.a, 8);
    std::memcpy(&child.uid_hi, &frame.b, 8);
    child.uid_ok = true;
  }
  if (type == IpcType::kChildReady || type == IpcType::kUidReport) {
    child.ready = true;
  }
  if (type == IpcType::kUdpProof) {
    auto proof = UnpackUdpProofFrame(frame);
    auto const path = static_cast<UdpProofPath>(frame.event_kind);
    if (path == UdpProofPath::kOwn) {
      child.own_proof = proof;
      child.got_own_proof = true;
    } else if (path == UdpProofPath::kDestination) {
      child.dest_proof = proof;
      child.got_dest_proof = true;
    }
  }
  if (type == IpcType::kPingTrace) {
    BobPingEvent e{};
    e.kind = frame.event_kind;
    e.server_id = frame.a;
    e.planned_us = frame.b;
    e.actual_us = frame.c;
    e.early_by_us = frame.d;
    e.base_window_us = frame.e;
    e.effective_window_us = frame.f;
    e.required_until_us = frame.g;
    e.next_planned_us = frame.h;
    e.guard_us = frame.i;
    e.channel_generation = frame.j;
    e.min_rtt_us = frame.k;
    e.p99_rtt_us = frame.l;
    e.result_type = static_cast<std::int64_t>(frame.offset_ms);
    e.event_steady_us = frame.local_steady_us;
    child.ping_events.push_back(e);
  }
}

bool SpawnChild(ChildProc& child, CoordinatorArgs const& args,
                std::string const& state_dir, std::string const& pipe_name,
                std::string const& client_name,
                std::string const& child_log_path) {
  if (!child.pipe.Create(pipe_name)) {
    std::cerr << "CreateNamedPipe failed for " << pipe_name << "\n";
    return false;
  }
  auto cmd = "\"" + args.exe_path + "\" --role client --side " +
             std::string(child.side == Side::kA ? "A" : "B") + " --run-id " +
             args.run_id + " --state-dir \"" + state_dir + "\" --pipe \"" +
             pipe_name + "\" --client-name " + client_name + " --parent-uid " +
             args.parent_uid;
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE log = CreateFileA(child_log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log == INVALID_HANDLE_VALUE) {
    std::cerr << "CreateFile child log failed: " << child_log_path << "\n";
    return false;
  }
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = log;
  si.hStdError = log;
  std::vector<char> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back('\0');
  if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &child.pi)) {
    CloseHandle(log);
    std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
    return false;
  }
  CloseHandle(log);
  if (!child.pipe.WaitForClient(120000)) {
    std::cerr << "WaitForClient timeout side="
              << (child.side == Side::kA ? "A" : "B") << "\n";
    return false;
  }
  return true;
}

void StopChild(ChildProc& child) {
  SendCmd(child, IpcType::kShutdown);
  if (WaitForSingleObject(child.pi.hProcess, 15000) != WAIT_OBJECT_0) {
    TerminateProcess(child.pi.hProcess, 1);
  }
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
  child.pipe.Close();
}

double QpcToMs(std::uint64_t delta_ticks) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  return (1000.0 * static_cast<double>(delta_ticks)) /
         static_cast<double>(freq.QuadPart);
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  auto const idx = static_cast<std::size_t>(
      std::ceil(p * static_cast<double>(values.size() - 1)));
  return values[std::min(idx, values.size() - 1)];
}

}  // namespace

int RunCoordinator(CoordinatorArgs args) {
  if (args.run_id.empty()) {
    args.run_id = MakeRunId();
  }
  if (args.exe_path.empty()) {
    args.exe_path = DefaultExePath();
  }
  if (args.artifact_dir.empty()) {
    args.artifact_dir = ".artifacts/uap-delivery-timing/" + args.run_id;
  }

  std::filesystem::create_directories(args.artifact_dir);
  auto const state_root =
      std::filesystem::path{args.artifact_dir} / "persistent-state";
  auto const state_a = (state_root / "state-a").string();
  auto const state_b = (state_root / "state-b").string();
  std::filesystem::create_directories(state_a);
  std::filesystem::create_directories(state_b);

  ChildProc alice;
  alice.side = Side::kA;
  ChildProc bob;
  bob.side = Side::kB;

  auto const pipe_a = PipeNameFor(args.run_id, Side::kA);
  auto const pipe_b = PipeNameFor(args.run_id, Side::kB);

  std::cout << "Spawning Alice/Bob run_id=" << args.run_id << std::endl;
  auto const log_a =
      (std::filesystem::path{args.artifact_dir} / "alice.log").string();
  auto const log_b =
      (std::filesystem::path{args.artifact_dir} / "bob.log").string();
  if (!SpawnChild(alice, args, state_a, pipe_a, "uap-bench-alice", log_a) ||
      !SpawnChild(bob, args, state_b, pipe_b, "uap-bench-bob", log_b)) {
    return 2;
  }

  auto wait_ready = [&](ChildProc& c, char const* name) {
    auto const deadline = GetTickCount64() + 180000;
    while (GetTickCount64() < deadline && !(c.ready && c.uid_ok)) {
      if (auto f = c.pipe.TryReadFrame(200)) {
        HandleChildFrame(c, *f);
      }
    }
    if (!(c.ready && c.uid_ok)) {
      std::cerr << name << " not ready" << std::endl;
      return false;
    }
    return true;
  };
  if (!wait_ready(alice, "Alice") || !wait_ready(bob, "Bob")) {
    StopChild(alice);
    StopChild(bob);
    return 3;
  }

  std::int64_t a_lo = 0;
  std::int64_t a_hi = 0;
  std::int64_t b_lo = 0;
  std::int64_t b_hi = 0;
  std::memcpy(&a_lo, &alice.uid_lo, 8);
  std::memcpy(&a_hi, &alice.uid_hi, 8);
  std::memcpy(&b_lo, &bob.uid_lo, 8);
  std::memcpy(&b_hi, &bob.uid_hi, 8);
  SendCmd(alice, IpcType::kSetPeerUid, 0, 0, b_lo, b_hi);
  SendCmd(bob, IpcType::kSetPeerUid, 0, 0, a_lo, a_hi);
  // Drain acks
  for (int i = 0; i < 20; ++i) {
    if (auto f = alice.pipe.TryReadFrame(100)) {
      HandleChildFrame(alice, *f);
    }
    if (auto f = bob.pipe.TryReadFrame(100)) {
      HandleChildFrame(bob, *f);
    }
  }

  std::cout << "Waiting Bob warm-up (>=10 ping RTT samples)..." << std::endl;
  SendCmd(bob, IpcType::kWaitWarmup);
  std::int64_t warmup_n = 0;
  std::int64_t warmup_min = 0;
  std::int64_t warmup_p99 = 0;
  std::uint32_t warmup_guard = 0;
  {
    auto const deadline = GetTickCount64() + 300000;
    bool done = false;
    while (GetTickCount64() < deadline && !done) {
      if (auto f = bob.pipe.TryReadFrame(500)) {
        HandleChildFrame(bob, *f);
        if (static_cast<IpcType>(f->type) == IpcType::kWarmupDone) {
          warmup_n = f->a;
          warmup_min = f->b;
          warmup_p99 = f->c;
          warmup_guard = f->offset_ms;
          done = true;
        }
      }
      if (auto f = alice.pipe.TryReadFrame(0)) {
        HandleChildFrame(alice, *f);
      }
    }
    if (!done) {
      std::cerr << "Bob warm-up timed out\n";
      StopChild(alice);
      StopChild(bob);
      return 4;
    }
  }

  SendCmd(alice, IpcType::kWaitWarmup);
  std::int64_t alice_warmup_n = 0;
  std::int64_t alice_warmup_min = 0;
  std::int64_t alice_warmup_p99 = 0;
  std::int64_t alice_dest_server = 0;
  std::int64_t alice_dest_protocol = 0;
  {
    auto const deadline = GetTickCount64() + 300000;
    bool done = false;
    while (GetTickCount64() < deadline && !done) {
      if (auto f = alice.pipe.TryReadFrame(500)) {
        HandleChildFrame(alice, *f);
        if (static_cast<IpcType>(f->type) == IpcType::kWarmupDone) {
          alice_warmup_n = f->a;
          alice_warmup_min = f->b;
          alice_warmup_p99 = f->c;
          alice_dest_server = f->d;
          alice_dest_protocol = f->e;
          done = true;
        }
      }
      if (auto f = bob.pipe.TryReadFrame(0)) {
        HandleChildFrame(bob, *f);
      }
    }
    if (!done) {
      std::cerr << "Alice dest-server warm-up timed out\n";
      StopChild(alice);
      StopChild(bob);
      return 4;
    }
  }

  std::cout << "## Bob ping statistics\n"
            << "samples=" << warmup_n << " min_rtt_ms=" << warmup_min
            << " p99_rtt_ms=" << warmup_p99 << " guard_ms=" << warmup_guard
            << std::endl
            << std::endl;
  std::cout << "## Alice dest-server ping statistics\n"
            << "server_id=" << alice_dest_server << " samples=" << alice_warmup_n
            << " min_rtt_ms=" << alice_warmup_min
            << " p99_rtt_ms=" << alice_warmup_p99 << std::endl
            << std::endl;

  auto const dest_proto = static_cast<BenchProtocol>(alice_dest_protocol);
  std::cout << "AE_SUPPORT_TCP=" << AE_SUPPORT_TCP
            << " AE_SUPPORT_UDP=" << AE_SUPPORT_UDP
            << " selected alice=" << BenchProtocolName(alice.own_proof.protocol)
            << " bob=" << BenchProtocolName(bob.own_proof.protocol)
            << " dest=" << BenchProtocolName(dest_proto) << "\n\n";
  if (!IsMeasuredProtocolOk(alice.own_proof.protocol) ||
      !IsMeasuredProtocolOk(bob.own_proof.protocol) ||
      !IsMeasuredProtocolOk(dest_proto)) {
#if defined(AE_UAP_DELIVERY_REQUIRE_UDP) && AE_UAP_DELIVERY_REQUIRE_UDP
    std::cerr << "FAIL: measured work path is not UDP "
                 "(registration may be TCP)\n";
#else
    std::cerr << "FAIL: measured work path is not TCP\n";
#endif
    StopChild(alice);
    StopChild(bob);
    return 6;
  }

  std::map<std::uint32_t, SampleRecord> samples;
  std::uint32_t sequence = 1;
  auto const csv_path =
      (std::filesystem::path{args.artifact_dir} / "samples.csv").string();
  std::ofstream csv(csv_path);
  csv << "sequence,offset_ms,schedule_server_id,actual_send_server_id,"
         "route_generation,protocol,raw_next_ping_delta_ms,last_connect_delta_"
         "ms,query_send_us,one_way_estimate_us,converted_deadline_us,window_"
         "start_us,target_send_us,actual_send_us,receive_us,delivery_ms,"
         "duplicate_count,classification,valid,invalid_reason,"
         "bob_ping_server_id,bob_ping_planned_send_us,bob_ping_actual_send_us,"
         "bob_ping_early_by_us,bob_base_rx_window_us,"
         "bob_effective_wire_rx_window_us,bob_required_rx_until_us,"
         "bob_ping_result_us,bob_ping_guard_us\n";

  int route_invalid_total = 0;
  int duplicate_total = 0;
  bool acceptance_fail = false;
  for (int offset : kOffsetsMs) {
    int valid = 0;
    int attempts = 0;
    while (valid < kSamplesPerOffset && attempts < kSamplesPerOffset * 5) {
      ++attempts;
      auto const seq = sequence++;
      SendCmd(alice, IpcType::kRunSample, seq,
              static_cast<std::uint32_t>(offset));

      SampleRecord rec{};
      rec.sequence = seq;
      rec.offset_ms = offset;
      rec.classification = ClassificationForOffset(offset);
      bool got_send = false;
      bool got_recv = false;
      auto const deadline = GetTickCount64() + 45000;
      while (GetTickCount64() < deadline && !(got_send && got_recv)) {
        if (auto f = alice.pipe.TryReadFrame(100)) {
          auto const type = static_cast<IpcType>(f->type);
          auto const kind = static_cast<EventKind>(f->event_kind);
          if (type == IpcType::kSampleResult && f->sequence == seq) {
            if (kind == EventKind::kSampleSent) {
              rec.window_start_us = f->a;
              rec.converted_deadline_us = f->b;
              rec.send_qpc = static_cast<std::uint64_t>(f->c);
              rec.schedule_server_id = f->d;
              rec.actual_send_server_id = f->e;
              rec.route_generation = f->f;
              rec.protocol = f->g;
              rec.raw_next_ping_delta_ms = f->h;
              rec.last_connect_delta_ms = f->i;
              rec.query_send_us = f->j;
              rec.one_way_estimate_us = f->k;
              rec.target_send_us = f->l;
              rec.actual_send_us = f->local_steady_us;
              got_send = true;
            } else if (kind == EventKind::kSampleSkipped) {
              rec.window_start_us = f->a;
              rec.converted_deadline_us = f->b;
              rec.schedule_server_id = f->d;
              rec.actual_send_server_id = f->e;
              rec.route_generation = f->f;
              rec.protocol = f->g;
              rec.raw_next_ping_delta_ms = f->h;
              rec.last_connect_delta_ms = f->i;
              rec.invalid_reason = SkipReasonString(f->c);
              got_send = true;
              got_recv = true;
            } else if (kind == EventKind::kError) {
              rec.invalid_reason = "alice_error";
              got_send = true;
              got_recv = true;
            }
          }
        }
        if (auto f = bob.pipe.TryReadFrame(100)) {
          HandleChildFrame(bob, *f);
          auto const type = static_cast<IpcType>(f->type);
          auto const kind = static_cast<EventKind>(f->event_kind);
          if (type == IpcType::kEvent && kind == EventKind::kSampleReceived &&
              f->sequence == seq) {
            rec.send_qpc = static_cast<std::uint64_t>(f->a);
            rec.receive_qpc = static_cast<std::uint64_t>(f->b);
            rec.duplicate_count = static_cast<int>(f->c);
            rec.receive_us = f->local_steady_us;
            got_recv = true;
          }
        }
      }

      if (rec.invalid_reason.empty() && got_send && got_recv &&
          rec.receive_qpc >= rec.send_qpc) {
        rec.delivery_ms = QpcToMs(rec.receive_qpc - rec.send_qpc);
        if (rec.duplicate_count != 1) {
          rec.invalid_reason = rec.duplicate_count == 0 ? "no_receive_count"
                                                        : "duplicate";
          if (rec.duplicate_count > 1) {
            ++duplicate_total;
          }
        } else if (rec.schedule_server_id != rec.actual_send_server_id ||
                   rec.schedule_server_id == 0) {
          rec.invalid_reason = "ROUTE_MISMATCH";
          ++route_invalid_total;
        } else if (!IsMeasuredProtocolOk(
                       static_cast<BenchProtocol>(rec.protocol))) {
          rec.invalid_reason = "protocol_mismatch";
        } else {
          rec.valid = true;
          ++valid;
        }
      } else if (rec.invalid_reason.empty()) {
        rec.invalid_reason = "timeout_or_incomplete";
      }
      if (rec.invalid_reason == "INVALID_ROUTE_CHANGED" ||
          rec.invalid_reason == "ROUTE_MISMATCH") {
        ++route_invalid_total;
      }

      AttachAndClassify(rec, bob.ping_events);
      samples[seq] = rec;
      csv << rec.sequence << "," << rec.offset_ms << ","
          << rec.schedule_server_id << "," << rec.actual_send_server_id << ","
          << rec.route_generation << "," << rec.protocol << ","
          << rec.raw_next_ping_delta_ms << "," << rec.last_connect_delta_ms
          << "," << rec.query_send_us << "," << rec.one_way_estimate_us << ","
          << rec.converted_deadline_us << "," << rec.window_start_us << ","
          << rec.target_send_us << "," << rec.actual_send_us << ","
          << rec.receive_us << "," << rec.delivery_ms << ","
          << rec.duplicate_count << "," << rec.classification << ","
          << (rec.valid ? 1 : 0) << "," << rec.invalid_reason << ","
          << rec.bob_ping_server_id << "," << rec.bob_ping_planned_send_us
          << "," << rec.bob_ping_actual_send_us << ","
          << rec.bob_ping_early_by_us << "," << rec.bob_base_rx_window_us << ","
          << rec.bob_effective_wire_rx_window_us << ","
          << rec.bob_required_rx_until_us << "," << rec.bob_ping_result_us
          << "," << rec.bob_ping_guard_us << "\n";
      csv.flush();
      std::cout << "offset=" << offset << " seq=" << seq
                << " valid=" << rec.valid << " delivery_ms=" << rec.delivery_ms
                << " sched=" << rec.schedule_server_id
                << " actual=" << rec.actual_send_server_id
                << " reason=" << rec.invalid_reason << std::endl;
    }
    if (valid < kMinValidPerOffset) {
      std::cerr << "Only " << valid << " valid samples for offset " << offset
                << " (need " << kMinValidPerOffset << ")\n";
      acceptance_fail = true;
    }
  }

  std::cout << "\n## Delivery results\n";
  std::cout << "| offset_ms | valid | route_invalid | min_ms | p50_ms | p90_ms "
               "| max_ms | duplicates | classification |\n";
  std::cout << "|-----------|-------|---------------|--------|--------|--------|"
               "--------|------------|----------------|\n";
  for (int offset : kOffsetsMs) {
    std::vector<double> vals;
    int route_invalid = 0;
    int extras = 0;
    for (auto const& [_, s] : samples) {
      if (s.offset_ms != offset) {
        continue;
      }
      if (s.valid) {
        vals.push_back(s.delivery_ms);
      }
      if (s.invalid_reason == "INVALID_ROUTE_CHANGED" ||
          s.invalid_reason == "ROUTE_MISMATCH") {
        ++route_invalid;
      }
      if (s.duplicate_count > 1) {
        extras += s.duplicate_count - 1;
      }
    }
    double min_v = 0;
    double max_v = 0;
    if (!vals.empty()) {
      min_v = *std::min_element(vals.begin(), vals.end());
      max_v = *std::max_element(vals.begin(), vals.end());
    }
    auto p50 = Percentile(vals, 0.50);
    auto p90 = Percentile(vals, 0.90);
    std::cout << "| " << offset << " | " << vals.size() << " | " << route_invalid
              << " | " << min_v << " | " << p50 << " | " << p90 << " | "
              << max_v << " | " << extras << " | "
              << ClassificationForOffset(offset) << " |\n";
    if (static_cast<int>(vals.size()) < kMinValidPerOffset || extras != 0) {
      acceptance_fail = true;
    }
  }
  std::cout << "\nCSV: " << csv_path << std::endl;
  std::cout << "route_invalid_total=" << route_invalid_total
            << " duplicate_total=" << duplicate_total << std::endl;

#if defined(AE_UAP_DELIVERY_REQUIRE_UDP) && AE_UAP_DELIVERY_REQUIRE_UDP
  std::cout << "\n## UDP anomaly traces (if any)\n";
  bool saw_500_3641 = false;
  bool saw_1500_286 = false;
  for (auto const& [_, s] : samples) {
    if (!s.valid) {
      continue;
    }
    bool interesting = false;
    if (s.offset_ms == 500 && s.delivery_ms > 3000) {
      interesting = true;
      saw_500_3641 = true;
    }
    if (s.offset_ms == 1500 && s.delivery_ms < 500) {
      interesting = true;
      saw_1500_286 = true;
    }
    if (!interesting) {
      continue;
    }
    std::cout << "TRACE seq=" << s.sequence << " offset=" << s.offset_ms
              << " sched=" << s.schedule_server_id
              << " actual=" << s.actual_send_server_id
              << " gen=" << s.route_generation << " proto=" << s.protocol
              << " raw_delta_ms=" << s.raw_next_ping_delta_ms
              << " window_start_us=" << s.window_start_us
              << " target_us=" << s.target_send_us
              << " send_us=" << s.actual_send_us
              << " recv_us=" << s.receive_us
              << " delivery_ms=" << s.delivery_ms
              << " class=" << s.classification
              << " early_by_us=" << s.bob_ping_early_by_us
              << " base_win_us=" << s.bob_base_rx_window_us
              << " eff_win_us=" << s.bob_effective_wire_rx_window_us
              << " planned_us=" << s.bob_ping_planned_send_us
              << " ping_actual_us=" << s.bob_ping_actual_send_us
              << " required_until_us=" << s.bob_required_rx_until_us << "\n";
  }
  int explained_early = 0;
  int unexplained_1500 = 0;
  for (auto const& [_, s] : samples) {
    if (!s.valid || s.offset_ms != 1500 || s.delivery_ms >= 500) {
      continue;
    }
    if (s.classification == "EARLY_PING_EXTENDED_WINDOW") {
      ++explained_early;
    } else {
      ++unexplained_1500;
    }
  }
  std::cout << "reproduced_+500_~3641ms=" << (saw_500_3641 ? "yes" : "no")
            << "\nreproduced_+1500_~286ms=" << (saw_1500_286 ? "yes" : "no")
            << "\nexplained_by_early_ping_window=" << explained_early
            << "\nunexplained_+1500_short=" << unexplained_1500 << "\n";
  if (saw_500_3641) {
    acceptance_fail = true;
  }
#endif

  {
    auto const ping_csv =
        (std::filesystem::path{args.artifact_dir} / "bob_ping_trace.csv")
            .string();
    std::ofstream out(ping_csv);
    out << "kind,server_id,planned_us,actual_us,early_by_us,base_window_us,"
           "effective_window_us,required_until_us,next_planned_us,guard_us,"
           "min_rtt_us,p99_rtt_us,channel_generation,result_type,"
           "event_steady_us\n";
    for (auto const& e : bob.ping_events) {
      out << static_cast<int>(e.kind) << "," << e.server_id << ","
          << e.planned_us << "," << e.actual_us << "," << e.early_by_us << ","
          << e.base_window_us << "," << e.effective_window_us << ","
          << e.required_until_us << "," << e.next_planned_us << ","
          << e.guard_us << "," << e.min_rtt_us << "," << e.p99_rtt_us << ","
          << e.channel_generation << "," << e.result_type << ","
          << e.event_steady_us << "\n";
    }
  }

  StopChild(alice);
  StopChild(bob);
  if (acceptance_fail || route_invalid_total != 0 || duplicate_total != 0) {
    std::cerr << "FAIL: delivery acceptance checks\n";
    return 7;
  }
  return 0;
}

}  // namespace ae::bench::uap
