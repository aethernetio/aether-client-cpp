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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#if defined(RegisterClass)
#  undef RegisterClass
#endif

#include "common/bench_ipc.h"
#include "common/bench_types.h"
#include "common/udp_proof_types.h"

#include "aether/config.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/ae_actions/ping_test_faults.h"
#include "aether/receive_schedule.h"

namespace ae::test_uap_ping_retry_window {

#if AE_ENABLE_PING_TEST_FAULTS
using ae::PingFaultMode;
#endif
using ae::PingTraceKind;
using ae::bench::uap::BenchProtocol;
using ae::bench::uap::ChannelProof;
using ae::bench::uap::EventKind;
using ae::bench::uap::IpcFrame;
using ae::bench::uap::IpcType;
using ae::bench::uap::NamedPipeServer;
using ae::bench::uap::PipeNameFor;
using ae::bench::uap::UdpProofPath;
using ae::bench::uap::UnpackUdpProofFrame;
using IpcSide = ae::bench::uap::Side;

namespace {
constexpr std::uint8_t kIpcArmFault = 13;
constexpr std::uint8_t kIpcSendTagged = 14;
constexpr std::uint8_t kIpcQueryNow = 15;
constexpr std::uint8_t kIpcPingTraceEx = 16;
constexpr std::uint8_t kIpcAnnounceUnknown = 17;
constexpr std::uint8_t kIpcScheduleState = 18;
constexpr std::uint8_t kIpcPingBudget = 19;

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
  std::int64_t logical_cycle_id{0};
  std::int64_t physical_attempt_index{0};
  std::int64_t fault_mode{0};
  std::int64_t wire_next_connect_ms{0};
  std::int64_t cycle_anchor_us{0};
  std::int64_t contract_deadline_us{0};
  std::int64_t next_local_send_us{0};
  std::int64_t request_was_sent{0};
  std::int64_t response_was_ignored{0};
  std::int64_t event_qpc{0};
  std::int64_t attempt_lead_us{0};
  std::int64_t retry_reserve_us{0};
  std::int64_t loss_timeout_us{0};
  std::int64_t predeadline_retry_guaranteed{1};
};

struct ScheduleSnap {
  std::int64_t state{-2};
  std::int64_t next_us{0};
  std::int64_t last_online_us{0};
  std::int64_t selected{0};
  std::int64_t queried{0};
  std::int64_t successful{0};
  std::int64_t failed{0};
  std::int64_t skipped{0};
  std::int64_t qpc{0};
  std::int64_t steady_us{0};
};

struct SampleRec {
  std::string offset_name;
  std::int64_t offset_ms{0};
  std::int64_t window_ms{0};
  std::uint32_t sequence{0};
  std::int64_t tn_us{0};
  std::int64_t next_us{0};
  std::int64_t send_qpc{0};
  std::int64_t recv_qpc{0};
  std::int64_t recv_steady_us{0};
  std::int64_t schedule_server{0};
  std::int64_t actual_server{0};
  std::int64_t route_generation{0};
  std::int64_t protocol{0};
  std::int64_t raw_delta_ms{0};
  std::int64_t last_connect_ms{0};
  std::int64_t one_way_us{0};
  int recv_count{0};
  std::string classification{"LOST"};
  double delivery_ms{0};
  double deadline_error_ms{0};
  double window_end_slack_ms{0};
  bool premature{false};
};

struct CycleRec {
  int planned_fault{0};
  std::int64_t cycle_id{0};
  std::int64_t first_wire_next{0};
  std::int64_t retry_wire_next{0};
  std::int64_t tn_us{0};
  std::int64_t tn1_us{0};
  std::int64_t first_attempt_us{0};
  std::int64_t retry_attempt_us{0};
  std::int64_t timeout_us{0};
  std::int64_t timeout_qpc{0};
  std::int64_t retry_qpc{0};
  std::int64_t early_by_us{0};
  std::int64_t guard_us{0};
  std::int64_t retry_reserve_us{0};
  std::int64_t loss_timeout_us{0};
  std::int64_t attempt_lead_us{0};
  bool predeadline{true};
  bool retry_before_nominal{false};
  bool confirmed{false};
};

struct OfflineRec {
  std::string condition;
  double deadline_to_state_ms{0};
  double start_to_state_ms{0};
  int query_count{0};
  std::int64_t final_state{-2};
  bool false_state{false};
};

struct ChildProc {
  IpcSide side{};
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
  std::vector<ScheduleSnap> schedules;
  std::map<std::uint32_t, int> recv_counts;
  std::map<std::uint32_t, std::int64_t> recv_qpc;
  std::map<std::uint32_t, std::int64_t> recv_steady;
  std::map<std::uint32_t, std::int64_t> send_qpc;
  std::map<std::uint32_t, SampleRec> sent_samples;
  std::int64_t last_ack_a{0};
  std::int64_t last_ack_b{0};
  bool got_ack{false};
  bool warmup_done{false};
  std::int64_t warmup_n{0};
  std::int64_t warmup_min{0};
  std::int64_t warmup_p99{0};
  std::int64_t warmup_d{0};
  std::int64_t warmup_e{0};
  std::uint32_t warmup_guard{0};
};

void ResetChildRuntime(ChildProc& child) {
  child.uid_lo = 0;
  child.uid_hi = 0;
  child.ready = false;
  child.uid_ok = false;
  child.seq = 0;
  child.own_proof = {};
  child.dest_proof = {};
  child.got_own_proof = false;
  child.got_dest_proof = false;
  child.ping_events.clear();
  child.schedules.clear();
  child.recv_counts.clear();
  child.recv_qpc.clear();
  child.recv_steady.clear();
  child.send_qpc.clear();
  child.sent_samples.clear();
  child.got_ack = false;
  child.warmup_done = false;
}

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
  f.side = static_cast<std::uint8_t>(IpcSide::kCoordinator);
  f.seq = ++child.seq;
  f.sequence = sequence;
  f.offset_ms = offset_ms;
  f.a = a;
  f.b = b;
  f.c = c;
  return child.pipe.WriteFrame(f);
}

bool SendRaw(ChildProc& child, std::uint8_t type, std::uint32_t sequence = 0,
             std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0,
             std::int64_t d = 0, std::int64_t e = 0, std::int64_t f = 0) {
  IpcFrame frame{};
  frame.type = type;
  frame.side = static_cast<std::uint8_t>(IpcSide::kCoordinator);
  frame.seq = ++child.seq;
  frame.sequence = sequence;
  frame.a = a;
  frame.b = b;
  frame.c = c;
  frame.d = d;
  frame.e = e;
  frame.f = f;
  return child.pipe.WriteFrame(frame);
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
  if (type == IpcType::kAck) {
    child.got_ack = true;
    child.last_ack_a = frame.a;
    child.last_ack_b = frame.b;
  }
  if (type == IpcType::kWarmupDone) {
    child.warmup_done = true;
    child.warmup_n = frame.a;
    child.warmup_min = frame.b;
    child.warmup_p99 = frame.c;
    child.warmup_d = frame.d;
    child.warmup_e = frame.e;
    child.warmup_guard = frame.offset_ms;
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
  if (type == IpcType::kEvent &&
      static_cast<EventKind>(frame.event_kind) == EventKind::kSampleReceived) {
    auto count = static_cast<int>(frame.c);
    if (count <= 0) {
      count = 1;
    }
    child.recv_counts[frame.sequence] = count;
    child.recv_qpc[frame.sequence] = frame.b;
    child.recv_steady[frame.sequence] = frame.local_steady_us;
  }
  if (type == IpcType::kSampleResult &&
      static_cast<EventKind>(frame.event_kind) == EventKind::kSampleSent) {
    auto send_qpc = frame.c;
    if (send_qpc == 0) {
      send_qpc = frame.e;
    }
    if (send_qpc != 0) {
      child.send_qpc[frame.sequence] = send_qpc;
    }
    SampleRec rec{};
    rec.sequence = frame.sequence;
    rec.tn_us = frame.a;
    rec.next_us = frame.b;
    rec.send_qpc = send_qpc;
    rec.schedule_server = frame.d;
    rec.actual_server = frame.e;
    rec.route_generation = frame.f;
    rec.protocol = frame.g;
    rec.raw_delta_ms = frame.h;
    rec.last_connect_ms = frame.i;
    rec.one_way_us = frame.k;
    child.sent_samples[frame.sequence] = rec;
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
  if (frame.type == kIpcPingTraceEx && !child.ping_events.empty()) {
    auto& e = child.ping_events.back();
    e.logical_cycle_id = frame.a;
    e.physical_attempt_index = frame.b;
    e.fault_mode = frame.c;
    e.wire_next_connect_ms = frame.d;
    e.cycle_anchor_us = frame.e;
    e.contract_deadline_us = frame.f;
    e.next_local_send_us = frame.g;
    e.request_was_sent = frame.h;
    e.response_was_ignored = frame.i;
    e.event_qpc = frame.k;
    e.retry_reserve_us = frame.l;
  }
  if (frame.type == kIpcPingBudget && !child.ping_events.empty()) {
    auto& e = child.ping_events.back();
    e.attempt_lead_us = frame.a;
    e.retry_reserve_us = frame.b;
    e.loss_timeout_us = frame.c;
    e.predeadline_retry_guaranteed = frame.d;
    if (e.cycle_anchor_us == 0) {
      e.cycle_anchor_us = frame.e;
    }
    if (e.contract_deadline_us == 0) {
      e.contract_deadline_us = frame.f;
    }
    if (e.guard_us == 0) {
      e.guard_us = frame.g;
    }
  }
  if (frame.type == kIpcScheduleState) {
    ScheduleSnap s{};
    s.state = frame.a;
    s.next_us = frame.b;
    s.last_online_us = frame.c;
    s.selected = frame.d;
    s.queried = frame.e;
    s.successful = frame.f;
    s.failed = frame.g;
    s.skipped = frame.h;
    s.qpc = frame.i;
    s.steady_us = frame.local_steady_us;
    child.schedules.push_back(s);
  }
}

bool SpawnChild(ChildProc& child, CharacterizationArgs const& args,
                std::string const& state_dir, std::string const& pipe_name,
                std::string const& client_name,
                std::string const& child_log_path, std::int64_t interval_ms,
                std::int64_t window_ms) {
  if (!child.pipe.Create(pipe_name)) {
    std::cerr << "CreateNamedPipe failed for " << pipe_name << "\n";
    return false;
  }
  auto cmd = "\"" + args.exe_path + "\" --role client --side " +
             std::string(child.side == IpcSide::kA ? "A" : "B") + " --run-id " +
             args.run_id + " --state-dir \"" + state_dir + "\" --pipe \"" +
             pipe_name + "\" --client-name " + client_name + " --parent-uid " +
             args.parent_uid + " --ping-interval-ms " +
             std::to_string(interval_ms) + " --receive-window-ms " +
             std::to_string(window_ms);
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
              << (child.side == IpcSide::kA ? "A" : "B") << "\n";
    return false;
  }
  return true;
}

void StopChild(ChildProc& child) {
  if (child.pi.hProcess == nullptr) {
    child.pipe.Close();
    return;
  }
  SendCmd(child, IpcType::kShutdown);
  if (WaitForSingleObject(child.pi.hProcess, 15000) != WAIT_OBJECT_0) {
    TerminateProcess(child.pi.hProcess, 1);
  }
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
  child.pi = {};
  child.pipe.Close();
}

void KillChild(ChildProc& child) {
  if (child.pi.hProcess != nullptr) {
    TerminateProcess(child.pi.hProcess, 1);
    WaitForSingleObject(child.pi.hProcess, 10000);
    CloseHandle(child.pi.hThread);
    CloseHandle(child.pi.hProcess);
    child.pi = {};
  }
  child.pipe.Close();
  child.ready = false;
  child.uid_ok = false;
}

double QpcToMs(std::uint64_t delta_ticks) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  return (1000.0 * static_cast<double>(delta_ticks)) /
         static_cast<double>(freq.QuadPart);
}

std::int64_t QpcNow() {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return v.QuadPart;
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

std::string ClassifySample(SampleRec const& rec, std::int64_t dest,
                           std::vector<BobPingEvent> const& pings) {
  if (rec.recv_count > 1) {
    return "DUPLICATE";
  }
  if (rec.schedule_server != 0 && rec.actual_server != 0 &&
      rec.schedule_server != rec.actual_server) {
    return "ROUTE_CHANGED";
  }
  if (rec.recv_count <= 0) {
    return "LOST";
  }
  BobPingEvent const* covering = nullptr;
  for (auto const& p : pings) {
    if (p.kind != static_cast<std::uint8_t>(PingTraceKind::kRequestSent)) {
      continue;
    }
    if (dest != 0 && p.server_id != dest && p.server_id != rec.actual_server) {
      continue;
    }
    if (p.event_steady_us > rec.recv_steady_us) {
      continue;
    }
    auto const end = p.event_steady_us + p.effective_window_us;
    if (rec.recv_steady_us <= end) {
      covering = &p;
    }
  }
  if (covering == nullptr) {
    return "OUTSIDE_WINDOW_UNEXPECTED_DELIVERY";
  }
  auto const tn = covering->cycle_anchor_us;
  auto const w = covering->base_window_us;
  if (covering->physical_attempt_index >= 2) {
    return "WAITED_FOR_RETRY_WINDOW";
  }
  if (tn > 0 && rec.recv_steady_us < tn && covering->early_by_us > 0 &&
      covering->effective_window_us > covering->base_window_us) {
    return "EARLY_EXTENDED_WINDOW";
  }
  if (tn > 0 && w > 0 && rec.recv_steady_us >= tn &&
      rec.recv_steady_us <= tn + w) {
    return "BASE_WINDOW";
  }
  if (tn > 0 && rec.recv_steady_us > tn + w) {
    return "WAITED_FOR_NEXT_LOGICAL_WINDOW";
  }
  return "BASE_WINDOW";
}

void WriteMdTable(std::ostream& out, std::vector<std::string> const& header,
                  std::vector<std::vector<std::string>> const& rows) {
  out << "|";
  for (auto const& h : header) {
    out << " " << h << " |";
  }
  out << "\n|";
  for (std::size_t i = 0; i < header.size(); ++i) {
    out << " --- |";
  }
  out << "\n";
  for (auto const& row : rows) {
    out << "|";
    for (std::size_t i = 0; i < header.size(); ++i) {
      out << " " << (i < row.size() ? row[i] : "") << " |";
    }
    out << "\n";
  }
}

std::string F3(double v) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(3) << v;
  return os.str();
}

std::string I64(std::int64_t v) { return std::to_string(v); }

}  // namespace

int RunCharacterization(CharacterizationArgs const& in_args) {
  CharacterizationArgs args = in_args;
  if (args.run_id.empty()) {
    args.run_id = MakeRunId();
  }
  if (args.exe_path.empty()) {
    args.exe_path = DefaultExePath();
  }
  if (args.artifact_dir.empty()) {
    args.artifact_dir = "artifacts/uap-1s-characterization/" + args.run_id;
  }
  if (args.seed == 0) {
    args.seed = 1;
  }
  if (args.quick) {
    args.skip_long_characterization = true;
    args.window_samples_main = 0;
    args.window_samples_extra = 0;
  }

  std::filesystem::create_directories(args.artifact_dir);
  auto const state_root =
      std::filesystem::path{args.artifact_dir} / "persistent-state";
  auto const state_a = (state_root / "state-a").string();
  auto const state_b = (state_root / "state-b").string();
  std::filesystem::create_directories(state_a);
  std::filesystem::create_directories(state_b);

  ChildProc alice;
  alice.side = IpcSide::kA;
  ChildProc bob;
  bob.side = IpcSide::kB;

  auto pipe_a = PipeNameFor(args.run_id, IpcSide::kA);
  auto pipe_b = PipeNameFor(args.run_id, IpcSide::kB);

  std::cout << "Spawning Alice/Bob run_id=" << args.run_id
            << " interval_ms=" << args.ping_interval_ms
            << " window_ms=" << args.receive_window_ms
            << " seed=" << args.seed
            << (args.quick ? " quick=1" : "") << std::endl;
  std::cout << std::unitbuf;
  auto const log_a =
      (std::filesystem::path{args.artifact_dir} / "alice.log").string();
  auto const log_b =
      (std::filesystem::path{args.artifact_dir} / "bob.log").string();
  if (!SpawnChild(alice, args, state_a, pipe_a, "uap-1s-alice", log_a,
                  args.ping_interval_ms, args.receive_window_ms) ||
      !SpawnChild(bob, args, state_b, pipe_b, "uap-1s-bob", log_b,
                  args.ping_interval_ms, args.receive_window_ms)) {
    return 2;
  }

  auto drain = [&](DWORD slice_ms) {
    std::optional<IpcFrame> last;
    if (auto f = bob.pipe.TryReadFrame(slice_ms)) {
      HandleChildFrame(bob, *f);
      last = f;
    }
    for (;;) {
      bool got = false;
      if (auto f = bob.pipe.TryReadFrame(0)) {
        HandleChildFrame(bob, *f);
        last = f;
        got = true;
      }
      if (auto f = alice.pipe.TryReadFrame(0)) {
        HandleChildFrame(alice, *f);
        last = f;
        got = true;
      }
      if (!got) {
        break;
      }
    }
    return last;
  };

  auto wait_ready = [&](ChildProc& c, char const* name) {
    auto const deadline = GetTickCount64() + 180000;
    while (GetTickCount64() < deadline && !(c.ready && c.uid_ok)) {
      drain(200);
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

  auto exchange_uids = [&]() {
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
    for (int i = 0; i < 20; ++i) {
      drain(100);
    }
  };
  exchange_uids();

  auto wait_warmup = [&](ChildProc& c, bool is_alice, std::int64_t* n,
                         std::int64_t* min_rtt, std::int64_t* p99,
                         std::uint32_t* guard, std::int64_t* server,
                         std::int64_t* proto) {
    c.warmup_done = false;
    SendCmd(c, IpcType::kWaitWarmup);
    auto const deadline = GetTickCount64() + 300000;
    while (GetTickCount64() < deadline) {
      drain(500);
      if (c.warmup_done) {
        *n = c.warmup_n;
        *min_rtt = c.warmup_min;
        *p99 = c.warmup_p99;
        if (is_alice) {
          if (server != nullptr) {
            *server = c.warmup_d;
          }
          if (proto != nullptr) {
            *proto = c.warmup_e;
          }
        } else if (guard != nullptr) {
          *guard = c.warmup_guard;
        }
        return true;
      }
    }
    return false;
  };

  std::int64_t warmup_n = 0;
  std::int64_t warmup_min = 0;
  std::int64_t warmup_p99 = 0;
  std::uint32_t warmup_guard = 0;
  std::int64_t alice_n = 0;
  std::int64_t alice_min = 0;
  std::int64_t alice_p99 = 0;
  std::int64_t dest = 0;
  std::int64_t dest_proto = 0;
  std::cout << "Waiting Bob/Alice warm-up..." << std::endl;
  if (!wait_warmup(bob, false, &warmup_n, &warmup_min, &warmup_p99,
                   &warmup_guard, nullptr, nullptr) ||
      !wait_warmup(alice, true, &alice_n, &alice_min, &alice_p99, nullptr,
                   &dest, &dest_proto)) {
    std::cerr << "warm-up timed out\n";
    StopChild(alice);
    StopChild(bob);
    return 4;
  }

  std::cout << "## Bob ping statistics\nsamples=" << warmup_n
            << " min_rtt_ms=" << warmup_min << " p99_rtt_ms=" << warmup_p99
            << " guard_ms=" << warmup_guard << " dest_server=" << dest
            << std::endl;
  auto const proto = static_cast<BenchProtocol>(dest_proto);
  if (!IsMeasuredProtocolOk(alice.own_proof.protocol) ||
      !IsMeasuredProtocolOk(bob.own_proof.protocol) ||
      !IsMeasuredProtocolOk(proto)) {
    std::cerr << "FAIL: measured work path protocol mismatch\n";
    StopChild(alice);
    StopChild(bob);
    return 6;
  }

#if !AE_ENABLE_PING_TEST_FAULTS
  std::cerr << "AE_ENABLE_PING_TEST_FAULTS is required\n";
  StopChild(alice);
  StopChild(bob);
  return 2;
#else
  bool ok = true;
  std::size_t ping_cursor = 0;
  auto wait_ping = [&](std::uint8_t kind, DWORD timeout_ms)
      -> std::optional<BobPingEvent> {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      drain(50);
      while (ping_cursor < bob.ping_events.size()) {
        auto const& e = bob.ping_events[ping_cursor++];
        if (e.kind == kind && (dest == 0 || e.server_id == dest)) {
          return e;
        }
      }
    }
    return std::nullopt;
  };

  auto window_open = [&]() {
    std::int64_t last_sent = -1;
    std::int64_t last_closed = -1;
    for (auto const& e : bob.ping_events) {
      if (e.server_id != dest) {
        continue;
      }
      if (e.kind == static_cast<std::uint8_t>(PingTraceKind::kRequestSent) ||
          e.kind == static_cast<std::uint8_t>(PingTraceKind::kRequestDropped)) {
        last_sent = e.event_steady_us;
      }
      if (e.kind == static_cast<std::uint8_t>(PingTraceKind::kRxClosed)) {
        last_closed = e.event_steady_us;
      }
    }
    return last_sent > last_closed;
  };
  auto wait_window_closed = [&](DWORD timeout_ms) {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      drain(50);
      if (!window_open()) {
        return true;
      }
    }
    return !window_open();
  };

  auto arm_next = [&](std::int64_t mode, std::int64_t attempt, std::int64_t e,
                      std::int64_t timeout_us) {
    drain(50);
    SendRaw(bob, kIpcArmFault, 0, dest, attempt, mode, timeout_us, e, 0);
    drain(150);
  };

  std::uint32_t rng = args.seed;
  auto rnd = [&]() {
    rng = rng * 1664525u + 1013904223u;
    return rng;
  };
  int const n_nominal = args.logical_cycles < 0 ? 0 : args.logical_cycles;
  int n_drop = 0;
  int n_ignore = 0;
  bool const explicit_split =
      args.request_loss_cases >= 0 || args.response_loss_cases >= 0;
  if (explicit_split) {
    n_drop = args.request_loss_cases > 0 ? args.request_loss_cases : 0;
    n_ignore = args.response_loss_cases > 0 ? args.response_loss_cases : 0;
  } else if (args.loss_cases > 0) {
    n_drop = args.loss_cases;
    n_ignore = args.loss_cases;
  } else if (!args.quick && !args.skip_long_characterization) {
    n_drop = std::max(1, n_nominal / 10);
    n_ignore = n_drop;
    if (n_nominal >= 20) {
      n_drop = 10;
      n_ignore = 10;
    }
  }
  int const n_cycles =
      explicit_split ? (n_nominal + n_drop + n_ignore) : n_nominal;
  std::vector<int> fault_plan(static_cast<std::size_t>(n_cycles), 0);
  if (explicit_split) {
    for (int i = 0; i < n_drop && i < n_cycles; ++i) {
      fault_plan[static_cast<std::size_t>(i)] = 1;
    }
    for (int i = 0; i < n_ignore && n_drop + i < n_cycles; ++i) {
      fault_plan[static_cast<std::size_t>(n_drop + i)] = 2;
    }
  } else {
    for (int i = 0; i < n_drop && i < n_cycles; ++i) {
      fault_plan[static_cast<std::size_t>(i)] = 1;
    }
    for (int i = 0; i < n_ignore && n_drop + i < n_cycles; ++i) {
      fault_plan[static_cast<std::size_t>(n_drop + i)] = 2;
    }
  }
  for (int i = n_cycles - 1; i > 0; --i) {
    auto j = static_cast<int>(rnd() % static_cast<std::uint32_t>(i + 1));
    std::swap(fault_plan[static_cast<std::size_t>(i)],
              fault_plan[static_cast<std::size_t>(j)]);
  }

  std::cout << "COUNTS nominal=" << n_nominal << " request_loss=" << n_drop
            << " response_loss=" << n_ignore
            << " hard_stop=" << args.hard_stop_runs
            << " graceful=" << args.graceful_runs << std::endl;

  std::vector<CycleRec> cycles;
  cycles.reserve(static_cast<std::size_t>(n_cycles));
  std::int64_t first_tn = 0;
  std::int64_t first_tn1 = 0;
  std::int64_t prev_tn_us = 0;
  double phase_drift_max = 0;
  std::vector<double> phase_drifts;
  int invalid_metric_count = 0;
  int retries_before = 0;
  int retries_after = 0;
  int live_false_missed = 0;
  int live_false_unknown = 0;
  int query_failures = 0;
  int route_changes = 0;

  std::cout << "Running " << n_cycles << " logical cycles..." << std::endl;
  if (n_cycles > 0) {
    wait_window_closed(8000);
  }

  for (int ci = 0; ci < n_cycles; ++ci) {
    CycleRec rec{};
    rec.planned_fault = fault_plan[static_cast<std::size_t>(ci)];
    wait_window_closed(4000);
    if (rec.planned_fault != 0) {
      arm_next(rec.planned_fault, 1, 0, 0);
    } else {
      SendRaw(bob, kIpcArmFault, 0, dest, 1, 0, 0, 0, 0);
      drain(50);
    }

    auto started = wait_ping(
        static_cast<std::uint8_t>(PingTraceKind::kCycleStarted), 8000);
    if (!started) {
      started = wait_ping(
          static_cast<std::uint8_t>(PingTraceKind::kRequestSent), 8000);
    }
    if (!started) {
      started = wait_ping(
          static_cast<std::uint8_t>(PingTraceKind::kRequestDropped), 8000);
    }
    if (!started) {
      std::cerr << "FAIL cycle " << ci << ": no start\n";
      ok = false;
      break;
    }
    rec.cycle_id = started->logical_cycle_id;
    rec.tn_us = started->cycle_anchor_us;
    rec.tn1_us = started->contract_deadline_us;
    rec.first_attempt_us = started->actual_us != 0 ? started->actual_us
                                                   : started->event_steady_us;
    rec.first_wire_next = started->wire_next_connect_ms;
    rec.early_by_us = started->early_by_us;
    rec.guard_us = started->guard_us;
    rec.retry_reserve_us = started->retry_reserve_us;
    rec.loss_timeout_us = started->loss_timeout_us;
    rec.attempt_lead_us = started->attempt_lead_us;
    rec.predeadline = started->predeadline_retry_guaranteed != 0;

    std::optional<BobPingEvent> timeout_ev;
    if (rec.planned_fault != 0) {
      timeout_ev = wait_ping(
          static_cast<std::uint8_t>(PingTraceKind::kAttemptTimeout), 4000);
    }
    if (timeout_ev) {
      rec.timeout_us = timeout_ev->event_steady_us;
      rec.timeout_qpc = timeout_ev->event_qpc;
      auto retry = wait_ping(
          static_cast<std::uint8_t>(PingTraceKind::kRequestSent), 4000);
      if (!retry) {
        retry = wait_ping(
            static_cast<std::uint8_t>(PingTraceKind::kRequestDropped), 1000);
      }
      if (retry && retry->physical_attempt_index >= 2) {
        rec.retry_attempt_us = retry->event_steady_us;
        rec.retry_qpc = retry->event_qpc;
        rec.retry_wire_next = retry->wire_next_connect_ms;
        rec.retry_before_nominal =
            retry->wire_next_connect_ms > args.ping_interval_ms;
        if (rec.retry_before_nominal) {
          ++retries_before;
        } else {
          ++retries_after;
        }
      }
    }

    auto confirmed = wait_ping(
        static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 8000);
    rec.confirmed = confirmed.has_value();
    if (!rec.confirmed) {
      std::cerr << "WARN cycle " << ci << " not confirmed\n";
    }
    if (ci == 0) {
      first_tn = rec.tn_us;
      first_tn1 = rec.tn1_us;
      prev_tn_us = rec.tn_us;
      (void)first_tn;
      (void)first_tn1;
    } else if (rec.tn_us > 0 && prev_tn_us > 0) {
      auto const delta_us = rec.tn_us - prev_tn_us;
      auto const drift_ms =
          static_cast<double>(delta_us - args.ping_interval_ms * 1000) /
          1000.0;
      if (std::abs(drift_ms) < 2000.0) {
        phase_drifts.push_back(std::abs(drift_ms));
        phase_drift_max = std::max(phase_drift_max, std::abs(drift_ms));
      } else {
        ++invalid_metric_count;
      }
      prev_tn_us = rec.tn_us;
    }
    cycles.push_back(rec);

    if (ci % 5 == 0) {
      std::size_t const before = alice.schedules.size();
      SendRaw(alice, kIpcQueryNow);
      auto const q_deadline = GetTickCount64() + 2000;
      while (GetTickCount64() < q_deadline &&
             alice.schedules.size() == before) {
        drain(20);
      }
      if (alice.schedules.size() == before) {
        ++query_failures;
      } else {
        auto const& s = alice.schedules.back();
        if (s.state < 0) {
          ++query_failures;
        } else if (s.state == 1) {
          ++live_false_missed;
        } else if (s.state == 2) {
          ++live_false_unknown;
        }
      }
    }
  }

  auto collect_budget_from_cycles = [&](CycleRec& into) {
    for (auto const& c : cycles) {
      if (c.attempt_lead_us > 0 && c.early_by_us > 0) {
        into = c;
        return;
      }
    }
    for (auto const& c : cycles) {
      if (c.guard_us > 0 || c.attempt_lead_us > 0) {
        into = c;
        return;
      }
    }
  };
  CycleRec budget_row{};
  collect_budget_from_cycles(budget_row);

  struct OffsetSpec {
    char const* name;
    std::int64_t offset_ms;
  };
  auto make_offsets = [&](std::int64_t window_ms) {
    std::int64_t lead_ms = budget_row.attempt_lead_us / 1000;
    if (lead_ms <= 0) {
      lead_ms = static_cast<std::int64_t>(warmup_guard) + 80;
    }
    return std::vector<OffsetSpec>{
        {"EARLY_EXTENDED", -(lead_ms / 2)},
        {"EARLY_IN_WINDOW", 25},
        {"NEAR_WINDOW_END", window_ms - 20},
        {"JUST_OUTSIDE", window_ms + 20},
        {"BEFORE_NEXT_PING", args.ping_interval_ms - 50},
    };
  };

  std::vector<SampleRec> samples;
  std::uint32_t next_seq = 1000;
  auto run_window_samples = [&](std::int64_t window_ms, int per_offset) {
    auto const offsets = make_offsets(window_ms);
    for (auto const& off : offsets) {
      int got = 0;
      int attempts = 0;
      std::cout << "Window " << window_ms << " offset " << off.name
                << " ms=" << off.offset_ms << std::endl;
      while (got < per_offset && attempts < per_offset * 6) {
        ++attempts;
        wait_window_closed(3000);
        auto const seq = next_seq++;
        SendCmd(alice, IpcType::kRunSample, seq, 0, off.offset_ms);
        auto const deadline = GetTickCount64() + 20000;
        bool sent = false;
        SampleRec rec{};
        rec.offset_name = off.name;
        rec.offset_ms = off.offset_ms;
        rec.window_ms = window_ms;
        rec.sequence = seq;
        while (GetTickCount64() < deadline && !sent) {
          drain(50);
          auto it = alice.sent_samples.find(seq);
          if (it != alice.sent_samples.end()) {
            rec = it->second;
            rec.offset_name = off.name;
            rec.offset_ms = off.offset_ms;
            rec.window_ms = window_ms;
            sent = true;
          }
        }
        if (!sent) {
          continue;
        }
        auto const recv_deadline = GetTickCount64() + 8000;
        while (GetTickCount64() < recv_deadline) {
          drain(50);
          auto rit = bob.recv_counts.find(seq);
          if (rit != bob.recv_counts.end()) {
            rec.recv_count = rit->second;
            rec.recv_qpc = bob.recv_qpc[seq];
            rec.recv_steady_us = bob.recv_steady[seq];
            break;
          }
        }
        rec.classification = ClassifySample(rec, dest, bob.ping_events);
        if (rec.recv_qpc > 0 && rec.send_qpc > 0 &&
            rec.recv_qpc >= rec.send_qpc) {
          rec.delivery_ms =
              QpcToMs(static_cast<std::uint64_t>(rec.recv_qpc - rec.send_qpc));
        }
        std::int64_t bob_wire = 0;
        std::int64_t bob_rx = 0;
        std::int64_t bob_tn1 = 0;
        std::int64_t bob_tn = 0;
        for (auto const& p : bob.ping_events) {
          if (p.kind == static_cast<std::uint8_t>(PingTraceKind::kRequestSent) &&
              p.server_id == dest && p.cycle_anchor_us > 0) {
            bob_wire = p.wire_next_connect_ms;
            bob_rx = p.effective_window_us / 1000;
            bob_tn1 = p.contract_deadline_us;
            bob_tn = p.cycle_anchor_us;
          }
        }
        rec.deadline_error_ms =
            static_cast<double>(rec.raw_delta_ms - bob_wire);
        auto const est_end_ms = rec.raw_delta_ms + bob_rx;
        auto const nominal_end_from_query =
            rec.raw_delta_ms -
            (bob_tn1 > bob_tn ? (bob_tn1 - bob_tn) / 1000 : args.ping_interval_ms) +
            window_ms;
        rec.window_end_slack_ms =
            static_cast<double>(est_end_ms - (rec.raw_delta_ms -
                                              (args.ping_interval_ms - window_ms)));
        (void)nominal_end_from_query;
        rec.premature = rec.classification == "OUTSIDE_WINDOW_UNEXPECTED_DELIVERY";
        if (rec.schedule_server != rec.actual_server) {
          ++route_changes;
        }
        if (rec.classification != "LOST") {
          ++got;
        }
        samples.push_back(rec);
        std::cout << "  seq=" << seq << " class=" << rec.classification
                  << " recv=" << rec.recv_count << " got=" << got << "/"
                  << per_offset << std::endl;
      }
      std::cout << "  done " << off.name << " collected=" << got
                << " attempts=" << attempts << std::endl;
    }
  };

  if (args.window_samples_main > 0) {
    std::cout << "Window samples window=" << args.receive_window_ms << std::endl;
    run_window_samples(args.receive_window_ms, args.window_samples_main);
  }

  auto run_targeted = [&](int mode1, int mode2, char const* name) {
    std::cout << "Targeted " << name << std::endl;
    wait_window_closed(4000);
    arm_next(mode1, 1, 0, 0);
    if (mode2 != 0) {
      arm_next(mode2, 2, 1, 0);
    }
    auto started = wait_ping(
        static_cast<std::uint8_t>(PingTraceKind::kCycleStarted), 8000);
    (void)started;
    (void)wait_ping(static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed),
                    12000);
  };
  if (!args.quick && !args.skip_long_characterization) {
    run_targeted(1, 1, "double_drop");
    run_targeted(2, 2, "double_ignore");
    run_targeted(1, 2, "drop_then_ignore");
  }

  auto restart_pair = [&](std::int64_t window_ms, int suffix) -> bool {
    if (alice.pi.hProcess != nullptr) {
      StopChild(alice);
    }
    if (bob.pi.hProcess != nullptr) {
      StopChild(bob);
    }
    Sleep(1000);
    ResetChildRuntime(alice);
    ResetChildRuntime(bob);
    alice.side = IpcSide::kA;
    bob.side = IpcSide::kB;
    auto const pipe_a2 =
        PipeNameFor(args.run_id, IpcSide::kA, "a" + std::to_string(suffix));
    auto const pipe_b2 =
        PipeNameFor(args.run_id, IpcSide::kB, "b" + std::to_string(suffix));
    auto const log_a =
        (std::filesystem::path{args.artifact_dir} /
         ("alice-" + std::to_string(suffix) + ".log"))
            .string();
    auto const log_b =
        (std::filesystem::path{args.artifact_dir} /
         ("bob-" + std::to_string(suffix) + ".log"))
            .string();
    auto const state_a2 =
        (state_root / ("state-a-" + std::to_string(suffix))).string();
    auto const state_b2 =
        (state_root / ("state-b-" + std::to_string(suffix))).string();
    std::filesystem::create_directories(state_a2);
    std::filesystem::create_directories(state_b2);
    if (!SpawnChild(alice, args, state_a2, pipe_a2, "uap-1s-alice", log_a,
                    args.ping_interval_ms, window_ms) ||
        !SpawnChild(bob, args, state_b2, pipe_b2, "uap-1s-bob", log_b,
                    args.ping_interval_ms, window_ms)) {
      return false;
    }
    ping_cursor = 0;
    if (!wait_ready(alice, "Alice-restart") ||
        !wait_ready(bob, "Bob-restart")) {
      return false;
    }
    exchange_uids();
    if (args.quick) {
      auto const confirmed = wait_ping(
          static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), 20000);
      if (!confirmed) {
        std::cerr << "FAIL no confirmed ping after pair restart\n";
        return false;
      }
      std::cout << "Pair restarted (quick) window_ms=" << window_ms
                << " dest=" << dest << std::endl;
      return true;
    }
    std::int64_t n = 0;
    std::int64_t mn = 0;
    std::int64_t p99 = 0;
    std::uint32_t g = 0;
    if (!wait_warmup(bob, false, &n, &mn, &p99, &g, nullptr, nullptr)) {
      return false;
    }
    std::int64_t alice_n2 = 0;
    std::int64_t alice_min2 = 0;
    std::int64_t alice_p992 = 0;
    std::int64_t alice_srv = 0;
    std::int64_t alice_proto = 0;
    if (!wait_warmup(alice, true, &alice_n2, &alice_min2, &alice_p992, nullptr,
                     &alice_srv, &alice_proto)) {
      std::cerr << "FAIL Alice warmup after pair restart\n";
      return false;
    }
    if (alice_srv != 0) {
      dest = alice_srv;
    }
    warmup_guard = g;
    std::cout << "Pair restarted window_ms=" << window_ms << " dest=" << dest
              << " alice_samples=" << alice_n2 << std::endl;
    return true;
  };

  if (args.window_samples_extra > 0) {
    for (std::int64_t extra_w : {std::int64_t{100}, std::int64_t{500}}) {
      std::cout << "Extra window " << extra_w << " ms" << std::endl;
      if (!restart_pair(extra_w, static_cast<int>(extra_w))) {
        std::cerr << "FAIL restart pair for window " << extra_w << "\n";
        ok = false;
        break;
      }
      run_window_samples(extra_w, args.window_samples_extra);
    }
    if (!restart_pair(args.receive_window_ms, 250)) {
      std::cerr << "FAIL restore 250ms pair\n";
      ok = false;
    }
  }

  std::vector<OfflineRec> offline;
  auto poll_until_state = [&](std::int64_t want, DWORD timeout_ms, int* qcount,
                              std::int64_t* got_state, std::int64_t* got_qpc) {
    *qcount = 0;
    *got_state = -2;
    *got_qpc = 0;
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      std::size_t const before = alice.schedules.size();
      SendRaw(alice, kIpcQueryNow);
      ++*qcount;
      auto const wait_deadline = GetTickCount64() + 2000;
      while (GetTickCount64() < wait_deadline &&
             alice.schedules.size() == before) {
        drain(50);
      }
      if (alice.schedules.size() > before) {
        auto const& s = alice.schedules.back();
        *got_state = s.state;
        *got_qpc = s.qpc;
        if (s.state == want) {
          return true;
        }
      }
    }
    return false;
  };

  std::cout << "Hard-stop runs=" << args.hard_stop_runs << std::endl;
  for (int r = 0; r < args.hard_stop_runs; ++r) {
    int stable = 0;
    int const need_stable = args.quick ? 2 : 10;
    while (stable < need_stable) {
      if (wait_ping(static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed),
                    8000)) {
        ++stable;
      } else {
        break;
      }
    }
    std::size_t const before = alice.schedules.size();
    SendRaw(alice, kIpcQueryNow);
    auto const q_deadline = GetTickCount64() + 2000;
    while (GetTickCount64() < q_deadline && alice.schedules.size() == before) {
      drain(20);
    }
    ScheduleSnap last{};
    if (!alice.schedules.empty()) {
      last = alice.schedules.back();
      if (last.state == 1) {
        ++live_false_missed;
      }
      if (last.state == 2) {
        ++live_false_unknown;
      }
    }
    double remaining_ms = 0;
    if (last.next_us > last.steady_us) {
      remaining_ms =
          static_cast<double>(last.next_us - last.steady_us) / 1000.0;
    }
    DWORD poll_ms = args.quick ? 20000 : 45000;
    if (remaining_ms > 0 && remaining_ms < 60000) {
      auto const need =
          static_cast<DWORD>(remaining_ms + (args.quick ? 8000 : 15000));
      if (need > poll_ms) {
        poll_ms = need;
      }
    }
    if (args.quick && poll_ms > 25000) {
      poll_ms = 25000;
    }
    auto const kill_qpc = QpcNow();
    KillChild(bob);
    int qcount = 0;
    std::int64_t got_state = -2;
    std::int64_t got_qpc = 0;
    bool const hit =
        poll_until_state(1, poll_ms, &qcount, &got_state, &got_qpc);
    std::cout << "hard-stop " << r << " pre_state=" << last.state
              << " remaining_ms=" << remaining_ms << " poll_ms=" << poll_ms
              << " state=" << got_state << " hit=" << hit
              << " queries=" << qcount << std::endl;
    OfflineRec o{};
    o.condition = "hard_stop";
    o.query_count = qcount;
    o.final_state = got_state;
    auto const deadline_qpc_est =
        last.qpc + static_cast<std::int64_t>(remaining_ms *
                                             (static_cast<double>(QpcNow() - QpcNow() + 1)));
    (void)deadline_qpc_est;
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    auto const deadline_qpc =
        last.qpc +
        static_cast<std::int64_t>(remaining_ms * freq.QuadPart / 1000.0);
    if (hit && got_qpc > deadline_qpc) {
      o.deadline_to_state_ms =
          QpcToMs(static_cast<std::uint64_t>(got_qpc - deadline_qpc));
    } else if (hit && got_qpc > kill_qpc) {
      o.deadline_to_state_ms =
          QpcToMs(static_cast<std::uint64_t>(got_qpc - kill_qpc));
    }
    if (got_qpc > kill_qpc) {
      o.start_to_state_ms =
          QpcToMs(static_cast<std::uint64_t>(got_qpc - kill_qpc));
    }
    o.false_state = !hit;
    offline.push_back(o);
    if (!restart_pair(args.receive_window_ms, 1000 + r)) {
      std::cerr << "FAIL respawn after hard-stop " << r << "\n";
      ok = false;
      break;
    }
    Sleep(args.quick ? 1500 : 6000);
  }

  std::cout << "Graceful-close runs=" << args.graceful_runs << std::endl;
  for (int r = 0; r < args.graceful_runs; ++r) {
    int variant = 0;
    if (!args.quick && !args.skip_long_characterization) {
      if (r % 20 < 8) {
        variant = 0;
      } else if (r % 20 < 14) {
        variant = 1;
      } else {
        variant = 2;
      }
    }
    wait_window_closed(4000);
    if (variant == 1) {
      arm_next(1, 1, 0, 0);
    } else if (variant == 2) {
      arm_next(2, 1, 0, 0);
    }
    auto const start_qpc = QpcNow();
    alice.got_ack = false;
    SendRaw(bob, kIpcAnnounceUnknown);
    auto const ack_deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < ack_deadline && !alice.got_ack) {
      drain(50);
      if (bob.got_ack) {
        break;
      }
    }
    std::int64_t ping0_qpc = 0;
    for (auto it = bob.ping_events.rbegin(); it != bob.ping_events.rend();
         ++it) {
      if (it->server_id == dest && it->wire_next_connect_ms == 0 &&
          (it->kind == static_cast<std::uint8_t>(PingTraceKind::kRequestSent) ||
           it->kind ==
               static_cast<std::uint8_t>(PingTraceKind::kRequestDropped))) {
        ping0_qpc = it->event_qpc;
        break;
      }
    }
    int qcount = 0;
    std::int64_t got_state = -2;
    std::int64_t got_qpc = 0;
    bool const hit = poll_until_state(2, args.quick ? 8000 : 20000, &qcount,
                                      &got_state, &got_qpc);
    std::cout << "graceful " << r << " var=" << variant
              << " state=" << got_state << " hit=" << hit
              << " queries=" << qcount << std::endl;
    OfflineRec o{};
    if (variant == 0) {
      o.condition = "graceful_unknown";
    } else if (variant == 1) {
      o.condition = "drop_request";
    } else {
      o.condition = "ignore_response";
    }
    o.query_count = qcount;
    o.final_state = got_state;
    if (hit && got_qpc > start_qpc) {
      o.start_to_state_ms =
          QpcToMs(static_cast<std::uint64_t>(got_qpc - start_qpc));
    }
    if (hit && ping0_qpc != 0 && got_qpc > ping0_qpc) {
      o.deadline_to_state_ms =
          QpcToMs(static_cast<std::uint64_t>(got_qpc - ping0_qpc));
    }
    if (got_state == 1) {
      o.false_state = true;
    }
    if (!hit) {
      o.false_state = true;
    }
    offline.push_back(o);
    if (r + 1 < args.graceful_runs) {
      if (!restart_pair(args.receive_window_ms, 2000 + r)) {
        std::cerr << "FAIL respawn after graceful " << r << "\n";
        ok = false;
        break;
      }
      Sleep(args.quick ? 1500 : 6000);
    }
  }

  auto values_of = [&](std::string const& cond) {
    std::vector<double> v;
    for (auto const& o : offline) {
      if (o.condition == cond && o.deadline_to_state_ms > 0) {
        v.push_back(o.deadline_to_state_ms);
      }
    }
    return v;
  };

  int premature = 0;
  int duplicates = 0;
  int delivered_ok = 0;
  int window_n = 0;
  for (auto const& s : samples) {
    if (s.window_ms != args.receive_window_ms) {
      continue;
    }
    ++window_n;
    if (s.premature) {
      ++premature;
    }
    if (s.recv_count > 1) {
      ++duplicates;
    }
    if (s.classification == "EARLY_EXTENDED_WINDOW" ||
        s.classification == "BASE_WINDOW" ||
        s.classification == "WAITED_FOR_RETRY_WINDOW" ||
        s.classification == "WAITED_FOR_NEXT_LOGICAL_WINDOW") {
      ++delivered_ok;
    }
  }

  int drop_ok = 0;
  int drop_n = 0;
  int ign_ok = 0;
  int ign_n = 0;
  for (auto const& c : cycles) {
    if (c.planned_fault == 1) {
      ++drop_n;
      if (c.confirmed && c.retry_attempt_us != 0) {
        ++drop_ok;
      }
    }
    if (c.planned_fault == 2) {
      ++ign_n;
      if (c.confirmed) {
        ++ign_ok;
      }
    }
  }

  int hard_hit = 0;
  int hard_n = 0;
  int grace_hit = 0;
  int grace_n = 0;
  for (auto const& o : offline) {
    if (o.condition == "hard_stop") {
      ++hard_n;
      if (o.final_state == 1) {
        ++hard_hit;
      }
    }
    if (o.condition == "graceful_unknown") {
      ++grace_n;
      if (o.final_state == 2) {
        ++grace_hit;
      }
    }
  }

  auto const proto_name = BenchProtocolName(proto);
  std::ofstream report(std::filesystem::path{args.artifact_dir} / "report.md");
  report << "# UAP 1s timing characterization\n\n";
  report << "- protocol: " << proto_name << "\n";
  report << "- seed: " << args.seed << "\n";
  report << "- interval_ms: " << args.ping_interval_ms << "\n";
  report << "- base_window_ms: " << args.receive_window_ms << "\n";
  report << "- dest_server: " << dest << "\n";
  report << "- cycles: " << cycles.size() << "\n\n";

  report << "## Ping timing by transport\n\n";
  auto const guard_ms_report = [&]() -> std::int64_t {
    auto const from_budget = budget_row.guard_us / 1000;
    if (from_budget <= 10 && warmup_p99 > 50) {
      ++invalid_metric_count;
      return warmup_guard;
    }
    if (from_budget > 0) {
      return from_budget;
    }
    return warmup_guard;
  }();

  WriteMdTable(report,
               {"protocol", "cycles", "interval", "base_window", "min_rtt",
                "p99_rtt", "guard", "loss_timeout", "retry_reserve",
                "attempt_lead", "first_attempt_early_by",
                "predeadline_retry_guaranteed"},
               {{proto_name, I64(static_cast<std::int64_t>(cycles.size())),
                 I64(args.ping_interval_ms), I64(args.receive_window_ms),
                 I64(warmup_min), I64(warmup_p99), I64(guard_ms_report),
                 I64(budget_row.loss_timeout_us / 1000),
                 I64(budget_row.retry_reserve_us / 1000),
                 I64(budget_row.attempt_lead_us / 1000),
                 I64(budget_row.early_by_us / 1000),
                 budget_row.predeadline ? "true" : "false"}});

  auto deadline_errs = [&](int fault) {
    std::vector<double> v;
    for (auto const& s : samples) {
      if (s.window_ms != args.receive_window_ms) {
        continue;
      }
      (void)fault;
      v.push_back(s.deadline_error_ms);
    }
    return v;
  };
  auto de = deadline_errs(0);
  report << "\n## Deadline accuracy\n\n";
  WriteMdTable(report,
               {"protocol", "fault", "samples", "error_min", "error_p50",
                "error_p95", "error_max"},
               {{proto_name, "mixed", I64(static_cast<std::int64_t>(de.size())),
                 de.empty() ? "0" : F3(*std::min_element(de.begin(), de.end())),
                 F3(Percentile(de, 0.50)), F3(Percentile(de, 0.95)),
                 de.empty() ? "0"
                            : F3(*std::max_element(de.begin(), de.end()))}});

  report << "\n## Window accuracy\n\n";
  {
    std::vector<std::vector<std::string>> rows;
    std::map<std::string, std::vector<SampleRec*>> grouped;
    for (auto& s : samples) {
      grouped[std::to_string(s.window_ms) + "/" + s.offset_name + "/" +
              s.classification]
          .push_back(&s);
    }
    for (auto& [key, vec] : grouped) {
      std::vector<double> d;
      int prem = 0;
      int dup = 0;
      for (auto* s : vec) {
        if (s->delivery_ms > 0) {
          d.push_back(s->delivery_ms);
        }
        prem += s->premature ? 1 : 0;
        dup += s->recv_count > 1 ? 1 : 0;
      }
      rows.push_back({proto_name, I64(vec.front()->window_ms),
                      vec.front()->offset_name,
                      I64(static_cast<std::int64_t>(vec.size())),
                      vec.front()->classification, F3(Percentile(d, 0.50)),
                      F3(Percentile(d, 0.95)),
                      d.empty()
                          ? "0"
                          : F3(*std::max_element(d.begin(), d.end())),
                      I64(prem), I64(dup)});
    }
    WriteMdTable(report,
                 {"protocol", "window_ms", "offset", "samples", "classification",
                  "delivery_p50", "delivery_p95", "delivery_max", "premature",
                  "duplicates"},
                 rows);
  }

  std::vector<double> retry_delays;
  int retry_before_n = 0;
  int retry_n = 0;
  for (auto const& c : cycles) {
    if (c.timeout_us == 0 || c.retry_attempt_us == 0) {
      continue;
    }
    double delay_ms = -1;
    if (c.timeout_qpc != 0 && c.retry_qpc != 0 && c.retry_qpc > c.timeout_qpc) {
      delay_ms = QpcToMs(static_cast<std::uint64_t>(c.retry_qpc - c.timeout_qpc));
    } else if (c.retry_attempt_us > c.timeout_us) {
      delay_ms = static_cast<double>(c.retry_attempt_us - c.timeout_us) / 1000.0;
    }
    if (delay_ms >= 0 && delay_ms < 5000) {
      retry_delays.push_back(delay_ms);
      ++retry_n;
      if (c.retry_before_nominal) {
        ++retry_before_n;
      }
    } else {
      ++invalid_metric_count;
    }
  }
  report << "\n## Retry\n\n";
  WriteMdTable(
      report,
      {"protocol", "fault", "samples", "timeout_to_retry_p50",
       "timeout_to_retry_p95", "retry_before_nominal_percent", "phase_drift_p50",
       "phase_drift_max"},
      {{proto_name, "mixed", I64(retry_n), F3(Percentile(retry_delays, 0.50)),
        F3(Percentile(retry_delays, 0.95)),
        retry_n == 0
            ? "0"
            : F3(100.0 * static_cast<double>(retry_before_n) /
                 static_cast<double>(retry_n)),
        F3(Percentile(phase_drifts, 0.50)), F3(phase_drift_max)}});

  report << "\n## Offline\n\n";
  {
    std::vector<std::vector<std::string>> rows;
    for (auto const* cond :
         {"hard_stop", "graceful_unknown", "drop_request", "ignore_response",
          "double_drop"}) {
      auto v = values_of(cond);
      int false_n = 0;
      int n = 0;
      for (auto const& o : offline) {
        if (o.condition == cond) {
          ++n;
          false_n += o.false_state ? 1 : 0;
        }
      }
      rows.push_back({proto_name, cond, I64(n), F3(Percentile(v, 0.50)),
                      F3(Percentile(v, 0.95)),
                      v.empty() ? "0"
                                : F3(*std::max_element(v.begin(), v.end())),
                      I64(false_n)});
    }
    WriteMdTable(report,
                 {"protocol", "condition", "samples", "deadline_to_state_p50",
                  "deadline_to_state_p95", "deadline_to_state_max",
                  "false_state_count"},
                 rows);
  }

  report << "\n## Reliability\n\n";
  report << "- live_false_MissedDeadline: " << live_false_missed << "\n";
  report << "- live_false_Unknown: " << live_false_unknown << "\n";
  report << "- missed_deadline_detection_rate: "
         << (hard_n == 0 ? 0.0
                         : static_cast<double>(hard_hit) /
                               static_cast<double>(hard_n))
         << " (" << hard_hit << "/" << hard_n << ")\n";
  report << "- graceful_unknown_detection_rate: "
         << (grace_n == 0 ? 0.0
                          : static_cast<double>(grace_hit) /
                                static_cast<double>(grace_n))
         << " (" << grace_hit << "/" << grace_n << ")\n";
  report << "- single_request_loss_recovery: "
         << (drop_n == 0 ? 0.0
                         : static_cast<double>(drop_ok) /
                               static_cast<double>(drop_n))
         << " (" << drop_ok << "/" << drop_n << ")\n";
  report << "- single_response_loss_recovery: "
         << (ign_n == 0 ? 0.0
                        : static_cast<double>(ign_ok) / static_cast<double>(ign_n))
         << " (" << ign_ok << "/" << ign_n << ")\n";
  report << "- retries_before_nominal: " << retries_before << "\n";
  report << "- retries_after_nominal: " << retries_after << "\n";
  report << "- correct_window_delivery_rate: "
         << (window_n == 0 ? 0.0
                           : static_cast<double>(delivered_ok) /
                                 static_cast<double>(window_n))
         << "\n";
  report << "- premature_delivery_count: " << premature << "\n";
  report << "- duplicates: " << duplicates << "\n";
  report << "- p99_rtt_ms: " << warmup_p99 << "\n";
  report << "- guard_ms: " << guard_ms_report << "\n";
  report << "- query_failures: " << query_failures << "\n";
  report << "- route_changes: " << route_changes << "\n";
  if (!alice.schedules.empty()) {
    auto const& s = alice.schedules.back();
    report << "- coverage selected/queried/successful/failed/quarantined_skipped: "
           << s.selected << "/" << s.queried << "/" << s.successful << "/"
           << s.failed << "/" << s.skipped << "\n";
  }
  report << "- phase_drift_max_ms: " << F3(phase_drift_max) << "\n";
  report << "- invalid_metric_count: " << invalid_metric_count << "\n";
  report << "- predeadline_retry_guaranteed: "
         << (budget_row.predeadline ? "true" : "false") << "\n";
  if (!budget_row.predeadline) {
    report << "\n**Blocker:** pre-deadline retry not guaranteed. min_rtt_ms="
           << warmup_min << " p99_rtt_ms=" << warmup_p99
           << " guard_ms=" << guard_ms_report
           << " loss_timeout_ms=" << (budget_row.loss_timeout_us / 1000)
           << " retry_reserve_ms=" << (budget_row.retry_reserve_us / 1000)
           << " attempt_lead_ms=" << (budget_row.attempt_lead_us / 1000)
           << " interval_ms=" << args.ping_interval_ms << "\n";
    if (args.transport == "tcp" || args.transport == "TCP") {
      report << "KNOWN BLOCKER: TCP pre-deadline retry is limited by "
                "RTT/timeout policy; not fixed in this quick loop.\n";
    }
  }

  {
    std::ofstream csv(std::filesystem::path{args.artifact_dir} /
                      "bob_ping_trace.csv");
    csv << "kind,server_id,cycle,attempt,wire_next,tn_us,tn1_us,early_by_us,"
           "guard_us,retry_reserve_us,loss_timeout_us,attempt_lead_us,"
           "predeadline,event_steady_us,event_qpc\n";
    for (auto const& e : bob.ping_events) {
      csv << static_cast<int>(e.kind) << "," << e.server_id << ","
          << e.logical_cycle_id << "," << e.physical_attempt_index << ","
          << e.wire_next_connect_ms << "," << e.cycle_anchor_us << ","
          << e.contract_deadline_us << "," << e.early_by_us << "," << e.guard_us
          << "," << e.retry_reserve_us << "," << e.loss_timeout_us << ","
          << e.attempt_lead_us << "," << e.predeadline_retry_guaranteed << ","
          << e.event_steady_us << "," << e.event_qpc << "\n";
    }
  }
  {
    std::ofstream csv(std::filesystem::path{args.artifact_dir} / "samples.csv");
    csv << "window,offset_name,offset_ms,seq,class,delivery_ms,deadline_error_"
           "ms,recv_count,premature\n";
    for (auto const& s : samples) {
      csv << s.window_ms << "," << s.offset_name << "," << s.offset_ms << ","
          << s.sequence << "," << s.classification << "," << s.delivery_ms << ","
          << s.deadline_error_ms << "," << s.recv_count << ","
          << (s.premature ? 1 : 0) << "\n";
    }
  }

  std::cout << "report=" << (std::filesystem::path{args.artifact_dir} / "report.md")
            << std::endl;
  std::cout << (ok ? "PASS characterization" : "FAIL characterization")
            << std::endl;

  StopChild(alice);
  StopChild(bob);
  if (duplicates != 0) {
    return 7;
  }
  return ok ? 0 : 7;
#endif
}

}  // namespace ae::test_uap_ping_retry_window
