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

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include "common/deadline_ipc.h"
#include "common/deadline_types.h"

namespace ae::test_uap_peer_deadline {
namespace {

struct ChildProc {
  Side side{};
  NamedPipeServer pipe;
  PROCESS_INFORMATION pi{};
  std::uint64_t uid_lo{0};
  std::uint64_t uid_hi{0};
  bool ready{false};
  bool uid_ok{false};
  std::uint32_t seq{0};
  bool process_open{false};
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

bool SendCmd(ChildProc& child, IpcType type, std::int64_t a = 0,
             std::int64_t b = 0, std::int64_t c = 0, std::uint32_t code = 0) {
  IpcFrame f{};
  f.type = static_cast<std::uint8_t>(type);
  f.side = static_cast<std::uint8_t>(Side::kCoordinator);
  f.seq = ++child.seq;
  f.code = code;
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
    child.ready = true;
  }
  if (type == IpcType::kChildReady) {
    child.ready = true;
  }
}

bool SpawnChild(ChildProc& child, CoordinatorArgs const& args,
                std::string const& state_dir, std::string const& pipe_name,
                std::string const& client_name,
                std::string const& child_log_path,
                std::string const& artifact_dir, bool inherit_console) {
  if (!child.pipe.Create(pipe_name)) {
    std::cerr << "CreateNamedPipe failed for " << pipe_name << "\n";
    return false;
  }
  auto cmd = "\"" + args.exe_path + "\" --role client --side " +
             std::string(child.side == Side::kA ? "A" : "B") + " --run-id " +
             args.run_id + " --state-dir \"" + state_dir + "\" --pipe \"" +
             pipe_name + "\" --client-name " + client_name + " --parent-uid " +
             args.parent_uid + " --artifact-dir \"" + artifact_dir + "\"";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE log = INVALID_HANDLE_VALUE;
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  if (inherit_console) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  } else {
    log = CreateFileA(child_log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                      &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
      std::cerr << "CreateFile child log failed: " << child_log_path << "\n";
      return false;
    }
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
  }
  std::vector<char> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back('\0');
  ZeroMemory(&child.pi, sizeof(child.pi));
  if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &child.pi)) {
    if (log != INVALID_HANDLE_VALUE) {
      CloseHandle(log);
    }
    std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
    return false;
  }
  if (log != INVALID_HANDLE_VALUE) {
    CloseHandle(log);
  }
  child.process_open = true;
  child.ready = false;
  child.uid_ok = false;
  if (!child.pipe.WaitForClient(120000)) {
    std::cerr << "WaitForClient timeout side="
              << (child.side == Side::kA ? "A" : "B") << "\n";
    return false;
  }
  return true;
}

bool WaitReady(ChildProc& child, DWORD timeout_ms) {
  auto const deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    if (auto f = child.pipe.TryReadFrame(200)) {
      HandleChildFrame(child, *f);
    }
    if (child.ready && child.uid_ok) {
      return true;
    }
  }
  return false;
}

void HardKill(ChildProc& child) {
  if (!child.process_open) {
    return;
  }
  TerminateProcess(child.pi.hProcess, 1);
  WaitForSingleObject(child.pi.hProcess, 15000);
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
  child.process_open = false;
  child.pipe.Close();
  ZeroMemory(&child.pi, sizeof(child.pi));
}

void SoftStop(ChildProc& child) {
  if (!child.process_open) {
    return;
  }
  SendCmd(child, IpcType::kShutdown);
  if (WaitForSingleObject(child.pi.hProcess, 15000) != WAIT_OBJECT_0) {
    TerminateProcess(child.pi.hProcess, 1);
  }
  CloseHandle(child.pi.hThread);
  CloseHandle(child.pi.hProcess);
  child.process_open = false;
  child.pipe.Close();
}

bool ProcessAlive(ChildProc const& child) {
  if (!child.process_open) {
    return false;
  }
  return WaitForSingleObject(child.pi.hProcess, 0) == WAIT_TIMEOUT;
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
    args.artifact_dir = ".artifacts/uap-peer-deadline/" + args.run_id;
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
  auto const log_a = args.artifact_dir + "/alice.log";
  auto const log_b = args.artifact_dir + "/bob.log";

  std::cout << "Spawning Alice/Bob run_id=" << args.run_id << std::endl;
  if (!SpawnChild(alice, args, state_a, pipe_a, "uap-deadline-alice", log_a,
                  args.artifact_dir, true) ||
      !SpawnChild(bob, args, state_b, pipe_b, "uap-deadline-bob", log_b,
                  args.artifact_dir, false)) {
    return 2;
  }
  if (!WaitReady(alice, 180000) || !WaitReady(bob, 180000)) {
    std::cerr << "children not ready\n";
    SoftStop(alice);
    SoftStop(bob);
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

  if (!SendCmd(alice, IpcType::kSetPeerUid, b_lo, b_hi) ||
      !SendCmd(bob, IpcType::kSetPeerUid, a_lo, a_hi)) {
    std::cerr << "SetPeerUid failed\n";
    return 4;
  }
  (void)alice.pipe.TryReadFrame(5000);
  (void)bob.pipe.TryReadFrame(5000);

  if (!SendCmd(bob, IpcType::kStartCloud) ||
      !SendCmd(alice, IpcType::kStartCloud)) {
    std::cerr << "StartCloud failed\n";
    return 5;
  }
  (void)bob.pipe.TryReadFrame(5000);
  (void)alice.pipe.TryReadFrame(5000);

  if (!SendCmd(alice, IpcType::kRunTest)) {
    std::cerr << "RunTest failed\n";
    return 6;
  }

  int result = 1;
  auto const deadline = GetTickCount64() + 300000;
  while (GetTickCount64() < deadline) {
    if (auto f = alice.pipe.TryReadFrame(200)) {
      auto const type = static_cast<IpcType>(f->type);
      if (type == IpcType::kRequestBobKill) {
        std::cout << "Flushing Bob state then hard-kill pid="
                  << bob.pi.dwProcessId << std::endl;
        if (!SendCmd(bob, IpcType::kFlushState)) {
          std::cerr << "FlushState send failed\n";
          SoftStop(alice);
          return 15;
        }
        (void)bob.pipe.TryReadFrame(10000);
        HardKill(bob);
        if (ProcessAlive(bob)) {
          std::cerr << "Bob still alive after TerminateProcess\n";
          SoftStop(alice);
          return 7;
        }
        if (!SendCmd(alice, IpcType::kBobKilled)) {
          std::cerr << "BobKilled notify failed\n";
          SoftStop(alice);
          return 8;
        }
      } else if (type == IpcType::kRequestBobRestart) {
        std::cout << "Restarting Bob same state/UID" << std::endl;
        auto const pipe_b2 = PipeNameFor(args.run_id + "-r", Side::kB);
        if (!SpawnChild(bob, args, state_b, pipe_b2, "uap-deadline-bob",
                        args.artifact_dir + "/bob-restart.log",
                        args.artifact_dir, false)) {
          SoftStop(alice);
          return 9;
        }
        if (!WaitReady(bob, 180000)) {
          std::cerr << "Bob restart not ready\n";
          SoftStop(alice);
          HardKill(bob);
          return 10;
        }
        if (bob.uid_lo != static_cast<std::uint64_t>(b_lo) ||
            bob.uid_hi != static_cast<std::uint64_t>(b_hi)) {
          std::cerr << "Bob UID changed after restart\n";
          SoftStop(alice);
          SoftStop(bob);
          return 11;
        }
        if (!SendCmd(bob, IpcType::kSetPeerUid, a_lo, a_hi) ||
            !SendCmd(bob, IpcType::kStartCloud)) {
          SoftStop(alice);
          SoftStop(bob);
          return 12;
        }
        (void)bob.pipe.TryReadFrame(5000);
        (void)bob.pipe.TryReadFrame(5000);
        if (!SendCmd(alice, IpcType::kBobRestarted)) {
          SoftStop(alice);
          SoftStop(bob);
          return 13;
        }
      } else if (type == IpcType::kTestDone) {
        result = (f->code == 0) ? 0 : 1;
        if (f->code == 0) {
          std::cout << "false_missed_deadline=" << f->a
                    << " recovery_ms=" << f->b
                    << " deadline_late_by_ms=" << f->c << std::endl;
        }
        break;
      }
    }
    if (!ProcessAlive(alice)) {
      std::cerr << "Alice exited early\n";
      result = 14;
      break;
    }
  }

  SoftStop(alice);
  if (ProcessAlive(bob)) {
    SoftStop(bob);
  }
  return result;
}

}  // namespace ae::test_uap_peer_deadline
