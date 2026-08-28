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
#if defined(RegisterClass)
#  undef RegisterClass
#endif

#include "common/bench_ipc.h"
#include "common/bench_types.h"
#include "common/udp_proof_types.h"

#include "aether/config.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/ae_actions/ping_test_faults.h"

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
constexpr std::uint8_t kIpcPingTraceEx = 16;
constexpr std::uint32_t kTagRequestLossQueued = 1;
constexpr std::uint32_t kTagResponseLossFirstWindow = 2;
constexpr std::uint32_t kTagAfterRetryWindow = 3;


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
};

#if 0
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
#endif

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
  std::map<std::uint32_t, int> recv_counts;
  std::map<std::uint32_t, std::int64_t> recv_qpc;
  std::map<std::uint32_t, std::int64_t> send_qpc;
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
  f.side = static_cast<std::uint8_t>(IpcSide::kCoordinator);
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
  if (type == IpcType::kEvent &&
      static_cast<EventKind>(frame.event_kind) == EventKind::kSampleReceived) {
    auto count = static_cast<int>(frame.c);
    if (count <= 0) {
      count = 1;
    }
    child.recv_counts[frame.sequence] = count;
    child.recv_qpc[frame.sequence] = frame.b;
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

  if (frame.type == kIpcPingTraceEx) {
    if (!child.ping_events.empty()) {
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
    }
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
             std::string(child.side == IpcSide::kA ? "A" : "B") + " --run-id " +
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
              << (child.side == IpcSide::kA ? "A" : "B") << "\n";
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

int RunCoordinator(CoordinatorArgs const& in_args) {
  CoordinatorArgs args = in_args;
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
  alice.side = IpcSide::kA;
  ChildProc bob;
  bob.side = IpcSide::kB;

  auto const pipe_a = PipeNameFor(args.run_id, IpcSide::kA);
  auto const pipe_b = PipeNameFor(args.run_id, IpcSide::kB);

  std::cout << "Spawning Alice/Bob run_id=" << args.run_id
            << (args.quick ? " quick=1" : "") << std::endl;
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


  [[maybe_unused]] bool scenarios_ok = false;
#if !AE_ENABLE_PING_TEST_FAULTS
  std::cerr << "AE_ENABLE_PING_TEST_FAULTS is required for this test\n";
  StopChild(alice);
  StopChild(bob);
  return 2;
#else
  bool ok = true;
  auto send_raw = [&](ChildProc& child, std::uint8_t type, std::uint32_t sequence = 0,
                      std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0,
                      std::int64_t d = 0) {
    IpcFrame f{};
    f.type = type;
    f.side = static_cast<std::uint8_t>(IpcSide::kCoordinator);
    f.seq = ++child.seq;
    f.sequence = sequence;
    f.a = a;
    f.b = b;
    f.c = c;
    f.d = d;
    return child.pipe.WriteFrame(f);
  };

  auto drain = [&](DWORD slice_ms) {
    // Prefer Bob: Alice ping traces otherwise fill the 4KiB pipe and Bob
    // blocks in WriteFrame, so he never reads ArmFault.
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

  std::size_t ping_cursor = 0;
  auto wait_ping = [&](std::uint8_t kind, std::int64_t server_id, DWORD timeout_ms)
      -> std::optional<BobPingEvent> {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      drain(50);
      while (ping_cursor < bob.ping_events.size()) {
        auto const& e = bob.ping_events[ping_cursor++];
        if (e.kind == kind && (server_id == 0 || e.server_id == server_id)) {
          return e;
        }
      }
    }
    return std::nullopt;
  };
  auto find_ping = [&](std::uint8_t kind, std::int64_t server_id,
                       std::int64_t cycle_id, std::int64_t min_attempt,
                       std::size_t start) -> std::optional<BobPingEvent> {
    for (std::size_t i = start; i < bob.ping_events.size(); ++i) {
      auto const& e = bob.ping_events[i];
      if (e.kind != kind) {
        continue;
      }
      if (server_id != 0 && e.server_id != server_id) {
        continue;
      }
      if (cycle_id != 0 && e.logical_cycle_id != 0 &&
          e.logical_cycle_id != cycle_id) {
        continue;
      }
      if (e.physical_attempt_index < min_attempt) {
        continue;
      }
      return e;
    }
    return std::nullopt;
  };

  auto recv_total = [&](std::uint32_t tag) -> int {
    auto const it = bob.recv_counts.find(tag);
    return it == bob.recv_counts.end() ? 0 : it->second;
  };
  auto wait_until_recv = [&](std::uint32_t tag, int min_count,
                             DWORD timeout_ms) -> int {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      drain(50);
      int const n = recv_total(tag);
      if (n >= min_count) {
        return n;
      }
    }
    return recv_total(tag);
  };

  auto wait_sent = [&](std::uint32_t tag, DWORD timeout_ms) -> bool {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      if (auto f = alice.pipe.TryReadFrame(50)) {
        HandleChildFrame(alice, *f);
        auto const type = static_cast<IpcType>(f->type);
        auto const kind = static_cast<EventKind>(f->event_kind);
        if (type == IpcType::kSampleResult && f->sequence == tag) {
          if (kind == EventKind::kSampleSent) {
            return true;
          }
          if (kind == EventKind::kSampleSkipped) {
            return false;
          }
        }
      }
      drain(0);
    }
    return false;
  };

  auto const dest = alice_dest_server;
  std::cout << "dest_server_id=" << dest << " transport=" << args.transport
            << std::endl;

  auto window_open = [&]() -> bool {
    std::int64_t last_sent = -1;
    std::int64_t last_closed = -1;
    for (auto const& e : bob.ping_events) {
      if (e.server_id != dest) {
        continue;
      }
      if (e.kind == static_cast<std::uint8_t>(PingTraceKind::kRequestSent)) {
        last_sent = e.event_steady_us;
      }
      if (e.kind == static_cast<std::uint8_t>(PingTraceKind::kRxClosed)) {
        last_closed = e.event_steady_us;
      }
    }
    return last_sent > last_closed;
  };
  auto wait_window_closed = [&](DWORD timeout_ms) -> bool {
    auto const deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
      drain(50);
      if (!window_open()) {
        return true;
      }
    }
    return !window_open();
  };

  if (!wait_window_closed(8000)) {
    std::cerr << "WARN: dest RX window still open before drop-request" << std::endl;
  }
  Sleep(250);

  auto arm = [&](std::int64_t mode, std::int64_t timeout_us) {
    drain(50);
    send_raw(bob, kIpcArmFault, 0, dest, 1, mode, timeout_us);
    drain(200);
  };

  // Scenario 1: drop request
  arm(static_cast<std::int64_t>(PingFaultMode::kDropRequest), 400000);
  auto dropped = wait_ping(static_cast<std::uint8_t>(PingTraceKind::kRequestDropped),
                           dest, 20000);
  if (!dropped) {
    std::cerr << "FAIL drop-request: no REQUEST_DROPPED\n";
    ok = false;
  } else {
    std::size_t const after_drop = ping_cursor;
    Sleep(100);
    send_raw(alice, kIpcSendTagged, kTagRequestLossQueued);
    if (!wait_sent(kTagRequestLossQueued, 5000)) {
      std::cerr << "FAIL drop-request: Alice send skipped/timeout\n";
      ok = false;
    }
    auto timeout_ev =
        wait_ping(static_cast<std::uint8_t>(PingTraceKind::kAttemptTimeout), dest,
                  10000);
    std::optional<BobPingEvent> retry;
    auto const retry_deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < retry_deadline && !retry) {
      drain(50);
      retry = find_ping(static_cast<std::uint8_t>(PingTraceKind::kRequestSent),
                        dest, dropped->logical_cycle_id, 2, after_drop);
      if (!retry) {
        retry = find_ping(
            static_cast<std::uint8_t>(PingTraceKind::kAttemptPrepared), dest,
            dropped->logical_cycle_id, 2, after_drop);
      }
    }
    if (!timeout_ev || !retry) {
      std::cerr << "FAIL drop-request: missing timeout/retry\n";
      ok = false;
    } else {
      auto const retry_delay_ms =
          (retry->event_steady_us - timeout_ev->event_steady_us) / 1000;
      if (retry_delay_ms > 100) {
        std::cerr << "FAIL drop-request: retry delay " << retry_delay_ms
                  << "ms > 100ms\n";
        ok = false;
      }
      if (retry->wire_next_connect_ms <= 0) {
        std::cerr << "FAIL drop-request: wire_next_connect_ms="
                  << retry->wire_next_connect_ms << " (must not be 0)\n";
        ok = false;
      }
      if (dropped->wire_next_connect_ms > 0 &&
          retry->wire_next_connect_ms >= dropped->wire_next_connect_ms) {
        std::cerr << "FAIL drop-request: retry wire_next_connect_ms="
                  << retry->wire_next_connect_ms
                  << " did not shrink vs first=" << dropped->wire_next_connect_ms
                  << "\n";
        ok = false;
      }
    }
    int const got = wait_until_recv(kTagRequestLossQueued, 1, 4000);
    if (got != 1) {
      std::cerr << "FAIL drop-request: expected 1 receive, got " << got << "\n";
      ok = false;
    } else if (retry && retry->event_qpc != 0 &&
               bob.recv_qpc[kTagRequestLossQueued] <= retry->event_qpc) {
      std::cerr << "FAIL drop-request: message_receive_qpc="
                << bob.recv_qpc[kTagRequestLossQueued]
                << " is not after retry_request_sent_qpc=" << retry->event_qpc
                << " cycle=" << retry->logical_cycle_id
                << " attempt=" << retry->physical_attempt_index << "\n";
      ok = false;
    }
    auto confirmed =
        wait_ping(static_cast<std::uint8_t>(PingTraceKind::kCycleConfirmed), dest,
                  10000);
    auto next_sched = wait_ping(
        static_cast<std::uint8_t>(PingTraceKind::kNextCycleScheduled), dest, 5000);
    if (dropped && next_sched && dropped->next_local_send_us > 0 &&
        next_sched->next_local_send_us > 0) {
      auto const delta =
          next_sched->next_local_send_us - dropped->next_local_send_us;
      if (delta < -20000 || delta > 20000) {
        std::cerr << "FAIL drop-request: next local send phase shift delta_us="
                  << delta << "\n";
        ok = false;
      }
    }
    (void)confirmed;
    std::cout << "scenario drop-request " << (ok ? "progressing" : "failed")
              << std::endl;
  }

  // Scenario 2: ignore response (400ms timeout override)
  arm(static_cast<std::int64_t>(PingFaultMode::kIgnoreResponse), 400000);
  auto sent1 = wait_ping(static_cast<std::uint8_t>(PingTraceKind::kRequestSent),
                         dest, 20000);
  auto ignored =
      wait_ping(static_cast<std::uint8_t>(PingTraceKind::kResponseIgnored), dest,
                5000);
  if (!sent1 || !ignored || sent1->request_was_sent == 0) {
    std::cerr << "FAIL ignore-response: missing SENT/IGNORED\n";
    ok = false;
  } else {
    Sleep(100);
    send_raw(alice, kIpcSendTagged, kTagResponseLossFirstWindow);
    if (!wait_sent(kTagResponseLossFirstWindow, 5000)) {
      std::cerr << "FAIL ignore-response: Alice send failed\n";
      ok = false;
    }
    int const got = wait_until_recv(kTagResponseLossFirstWindow, 1, 3000);
    if (got != 1) {
      std::cerr << "FAIL ignore-response: expected receive in first window, got "
                << got << "\n";
      ok = false;
    }
    auto retry = wait_ping(static_cast<std::uint8_t>(PingTraceKind::kRequestSent),
                           dest, 10000);
    if (!retry || retry->wire_next_connect_ms <= 0) {
      std::cerr << "FAIL ignore-response: retry nextConnect missing/zero\n";
      ok = false;
    } else if (sent1->wire_next_connect_ms > 0 &&
               retry->wire_next_connect_ms >= sent1->wire_next_connect_ms) {
      std::cerr << "FAIL ignore-response: retry nextConnect="
                << retry->wire_next_connect_ms
                << " did not shrink vs first=" << sent1->wire_next_connect_ms
                << "\n";
      ok = false;
    }
    if (got == 1 && sent1 && retry && sent1->event_qpc != 0 &&
        retry->event_qpc != 0) {
      auto const recv_qpc = bob.recv_qpc[kTagResponseLossFirstWindow];
      if (!(sent1->event_qpc < recv_qpc && recv_qpc < retry->event_qpc)) {
        std::cerr << "FAIL ignore-response: recv_qpc=" << recv_qpc
                  << " not between first_sent=" << sent1->event_qpc
                  << " and retry_sent=" << retry->event_qpc << "\n";
        ok = false;
      }
    }
    int const dup = wait_until_recv(kTagResponseLossFirstWindow, 2, 500);
    if (dup >= 2) {
      std::cerr << "FAIL ignore-response: duplicate receive\n";
      ok = false;
    }
  }

  // Scenario 3: after retry window, before next logical ping
  if (!wait_window_closed(8000)) {
    std::cerr << "FAIL after-window: retry RX window did not close" << std::endl;
    ok = false;
  }
  Sleep(300);
  std::int64_t close_qpc = 0;
  for (auto it = bob.ping_events.rbegin(); it != bob.ping_events.rend(); ++it) {
    if (it->server_id == dest &&
        it->kind == static_cast<std::uint8_t>(PingTraceKind::kRxClosed)) {
      close_qpc = it->event_qpc;
      break;
    }
  }
  send_raw(alice, kIpcSendTagged, kTagAfterRetryWindow);
  if (!wait_sent(kTagAfterRetryWindow, 5000)) {
    std::cerr << "FAIL after-window: send failed\n";
    ok = false;
  }
  auto next_sent =
      wait_ping(static_cast<std::uint8_t>(PingTraceKind::kRequestSent), dest,
                20000);
  int const late = wait_until_recv(kTagAfterRetryWindow, 1, 8000);
  if (late != 1) {
    std::cerr << "FAIL after-window: expected 1 receive after next ping, got "
              << late << "\n";
    ok = false;
  }
  auto const send_qpc = alice.send_qpc[kTagAfterRetryWindow];
  auto const recv_qpc = bob.recv_qpc[kTagAfterRetryWindow];
  if (late == 1 && close_qpc != 0 && send_qpc != 0 && next_sent &&
      next_sent->event_qpc != 0) {
    if (!(send_qpc > close_qpc)) {
      std::cerr << "FAIL after-window: message_send_qpc=" << send_qpc
                << " is not after retry_window_close_qpc=" << close_qpc << "\n";
      ok = false;
    }
    if (!(recv_qpc >= next_sent->event_qpc)) {
      std::cerr << "FAIL after-window: message_receive_qpc=" << recv_qpc
                << " is before next_logical_ping_request_sent_qpc="
                << next_sent->event_qpc << "\n";
      ok = false;
    }
  }
  (void)next_sent;

  std::cout << (ok ? "PASS ping retry window scenarios"
                   : "FAIL ping retry window scenarios")
            << std::endl;
  scenarios_ok = ok;
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
#if AE_ENABLE_PING_TEST_FAULTS
  return scenarios_ok ? 0 : 7;
#else
  return 2;
#endif
}

}  // namespace ae::test_uap_ping_retry_window
