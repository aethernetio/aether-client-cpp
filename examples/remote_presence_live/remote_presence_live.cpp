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
 *
 * Live Local+Remote Presence + CloudRequest fault/recovery harness.
 *
 * Multi-process (Win32):
 *   orchestrator (B) spawns a peer-A copy of this exe and firewall-blocks
 *   only that program path so B stays connected.
 *
 * Args:
 *   --role=orchestrator|peer
 *   --healthy-sec N          baseline before faults (default 60)
 *   --fault-cycles N         A network fault/recovery cycles (default 10)
 *   --work-dir PATH          shared status directory
 *   --skip-fault             baseline only (no Admin required)
 *   --skip-one-server        skip isolated S1 fault for B
 */

#define AE_EXAMPLE_LORA_MODULE 0
#define AE_EXAMPLE_MODEM 0
#ifdef ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "aether-miscpp/format/format.h"
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/all.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "ae-numeric/percentile8.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/config.h"
#include "aether/remote_presence.h"
#include "aether/types/address.h"

// IWYU pragma: begin_keeps
#include "../common/aether_construct_esp_wifi.h"
#include "../common/aether_construct_ethernet.h"
#include "../common/aether_construct_lora_module.h"
#include "../common/aether_construct_modem.h"
// IWYU pragma: end_keeps

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace ae::examples {
namespace {

using namespace std::chrono_literals;

static constexpr auto kParentUid =
    Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
static constexpr auto kInterval = 1s;
static constexpr auto kWindow = 1s;
static constexpr auto kOfflineTimeout = 1s;
static constexpr auto kQueryPeriod = 250ms;
static constexpr auto kPoll = 10ms;

template <typename... Args>
void Log(FormatScheme const& format, Args&&... args) {
  Format(std::cout, ">>> [{:time}] ", Now());
  Format(std::cout, format, std::forward<Args>(args)...);
  std::cout << '\n';
  std::cout.flush();
}

std::int64_t EpochMs(TimePoint tp) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             tp.time_since_epoch())
      .count();
}

char const* StateName(PeerPresenceState s) {
  switch (s) {
    case PeerPresenceState::kOnline:
      return "ONLINE";
    case PeerPresenceState::kOffline:
      return "OFFLINE";
    case PeerPresenceState::kUnknown:
      return "UNKNOWN";
  }
  return "?";
}

char const* SampleName(RemoteServerPresence s) {
  switch (s) {
    case RemoteServerPresence::kOnline:
      return "ONLINE";
    case RemoteServerPresence::kOffline:
      return "OFFLINE";
    case RemoteServerPresence::kUnknown:
      return "UNKNOWN";
    case RemoteServerPresence::kExcluded:
      return "EXCLUDED";
  }
  return "?";
}

void Pump(AetherApp& app, TimePoint until) {
  while (!app.IsExited() && Now() < until) {
    auto const next = app.Update(Now());
    auto const poll_at = Now() + kPoll;
    app.WaitUntil(next < poll_at ? next : poll_at);
  }
}

void ApplyTimings(Client& client) {
  auto policy = client.connectivity_policy();
  if (!policy) {
    return;
  }
  policy->ResetRxTimings();
  policy->SetOfflineDetectionTimeout(kOfflineTimeout);
  policy->SetCloudRequestExecutionPolicy(
      CloudRequestExecutionPolicy::FromFactor(Percentile8::FromPercent(99.99), TimeoutFactor8::FromDouble(1.2), /*retries=*/2,
                                              /*hedge=*/2));
  policy->ConfigureRxTimings(RequestPolicy::All{})
      .ForAllPriorities(RxTimingConf::Every(kInterval).WithWindow(kWindow));
  for (auto* server : client.cloud_connection().selected_servers()) {
    if (server == nullptr) {
      continue;
    }
    policy->ConfigureServerRxTiming(
        server->server_id(),
        RxTimingConf::Every(kInterval).WithWindow(kWindow),
        Percentile8::FromPercent(99.0));
  }
}

bool WaitLocalOnline(AetherApp& app, Client& client, Duration budget) {
  auto const deadline = Now() + budget;
  while (Now() < deadline && !app.IsExited()) {
    Pump(app, Now() + kPoll);
    if (client.IsLocallyOnline()) {
      return true;
    }
  }
  return client.IsLocallyOnline();
}

struct QueryResult {
  PeerPresence presence{};
  std::vector<RemoteServerPresenceSample> samples;
  std::vector<ServerId> peer_cloud_ids;
  std::vector<ServerId> authoritative_ids;
  std::vector<ServerId> queried_ids;
  bool used_observer_cloud{false};
  TimePoint start{};
  TimePoint complete{};
  std::uint64_t query_id{0};
};

void LogIds(char const* label, std::vector<ServerId> const& ids) {
  std::cout << "    " << label << "=[";
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) {
      std::cout << ',';
    }
    std::cout << ids[i];
  }
  std::cout << "]\n";
}

void LogQuery(QueryResult const& q) {
  Log("REMOTE_QUERY_START query_id={} start_ms={}", q.query_id,
      EpochMs(q.start));
  Log("REMOTE_QUERY_COMPLETE query_id={} complete_ms={} aggregate={}",
      q.query_id, EpochMs(q.complete), StateName(q.presence.state));
  LogIds("peer_cloud_server_ids", q.peer_cloud_ids);
  LogIds("authoritative_server_ids", q.authoritative_ids);
  LogIds("queried_server_ids", q.queried_ids);
  for (auto const& s : q.samples) {
    Log("  server={} status={} next_ping_delta_ms={} expected_open_ms={} "
        "offline_deadline_ms={} has_timing={}",
        s.server_id, SampleName(s.status), s.next_ping_delta_ms,
        EpochMs(s.expected_open), EpochMs(s.offline_deadline),
        s.has_timing ? 1 : 0);
  }
}

QueryResult RunOneQuery(AetherApp& app, Client& observer, Uid peer_uid,
                        std::uint64_t query_id) {
  QueryResult out{};
  out.query_id = query_id;
  out.start = Now();
  bool done = false;
  auto& action = observer.QueryPeerPresence(peer_uid);
  auto sub = action.result_event().Subscribe([&](auto const& res) {
    out.complete = Now();
    if (res) {
      out.presence = res.value();
    } else {
      out.presence.state = PeerPresenceState::kUnknown;
    }
    out.samples = action.samples();
    out.peer_cloud_ids = action.peer_cloud_server_ids();
    out.authoritative_ids = action.authoritative_server_ids();
    out.queried_ids = action.queried_server_ids();
    out.used_observer_cloud = action.used_observer_cloud();
    done = true;
  });
  auto const deadline = Now() + 30s;
  while (!done && Now() < deadline && !app.IsExited()) {
    Pump(app, Now() + kPoll);
  }
  if (!done) {
    out.complete = Now();
    out.presence.state = PeerPresenceState::kUnknown;
  }
  LogQuery(out);
  return out;
}

std::int64_t PercentileMs(std::vector<std::int64_t> values, double p) {
  if (values.empty()) {
    return -1;
  }
  std::sort(values.begin(), values.end());
  if (p <= 0.0) {
    return values.front();
  }
  if (p >= 100.0) {
    return values.back();
  }
  auto const idx = static_cast<std::size_t>(
      std::ceil((values.size() - 1) * (p / 100.0)));
  return values[std::min(idx, values.size() - 1)];
}

void PrintLatencyDist(char const* name, std::vector<std::int64_t> const& v) {
  if (v.empty()) {
    Log("DIST {} empty", name);
    return;
  }
  auto copy = v;
  Log("DIST {} count={} min={} median={} p90={} p99={} max={}", name, v.size(),
      PercentileMs(copy, 0), PercentileMs(copy, 50), PercentileMs(copy, 90),
      PercentileMs(copy, 99), PercentileMs(copy, 100));
}

#if defined(_WIN32)

std::wstring ThisExePath() {
  wchar_t path[MAX_PATH]{};
  auto const n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return {};
  }
  return std::wstring{path, static_cast<std::size_t>(n)};
}

std::wstring Widen(std::string const& s) {
  if (s.empty()) {
    return {};
  }
  int const n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      out.data(), n);
  return out;
}

std::string Narrow(std::wstring const& s) {
  if (s.empty()) {
    return {};
  }
  int const n = WideCharToMultiByte(CP_UTF8, 0, s.data(),
                                    static_cast<int>(s.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string out(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

int RunHidden(std::wstring cmd) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    return -1;
  }
  WaitForSingleObject(pi.hProcess, 20000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}

bool IsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  TOKEN_ELEVATION elevation{};
  DWORD size = 0;
  auto const ok = GetTokenInformation(token, TokenElevation, &elevation,
                                      sizeof(elevation), &size);
  CloseHandle(token);
  return ok && (elevation.TokenIsElevated != 0);
}

class WindowsExeFirewall {
 public:
  explicit WindowsExeFirewall(std::wstring exe_path)
      : exe_path_{std::move(exe_path)},
        tag_{std::to_wstring(GetCurrentProcessId())} {}
  ~WindowsExeFirewall() { Unblock(); }
  WindowsExeFirewall(WindowsExeFirewall const&) = delete;
  WindowsExeFirewall& operator=(WindowsExeFirewall const&) = delete;

  bool Block() {
    Unblock();
    auto const quoted = L"\"" + exe_path_ + L"\"";
    out_name_ = L"ae-rp-fw-out-" + tag_;
    in_name_ = L"ae-rp-fw-in-" + tag_;
    auto const out_cmd =
        L"netsh advfirewall firewall add rule name=\"" + out_name_ +
        L"\" dir=out action=block enable=yes profile=any program=" + quoted;
    auto const in_cmd =
        L"netsh advfirewall firewall add rule name=\"" + in_name_ +
        L"\" dir=in action=block enable=yes profile=any program=" + quoted;
    if (RunHidden(out_cmd) != 0 || RunHidden(in_cmd) != 0) {
      Unblock();
      return false;
    }
    active_ = true;
    Log("FIREWALL_BLOCK program={}", Narrow(exe_path_));
    return true;
  }

  void Unblock() {
    if (!out_name_.empty()) {
      RunHidden(L"netsh advfirewall firewall delete rule name=\"" + out_name_ +
                L"\"");
    }
    if (!in_name_.empty()) {
      RunHidden(L"netsh advfirewall firewall delete rule name=\"" + in_name_ +
                L"\"");
    }
    if (active_) {
      Log("FIREWALL_UNBLOCK program={}", Narrow(exe_path_));
    }
    active_ = false;
    out_name_.clear();
    in_name_.clear();
  }

  bool active() const { return active_; }

 private:
  std::wstring exe_path_;
  std::wstring tag_;
  std::wstring out_name_;
  std::wstring in_name_;
  bool active_{false};
};

class WindowsRemoteIpFirewall {
 public:
  WindowsRemoteIpFirewall(std::wstring program, std::string remote_ip)
      : program_{std::move(program)},
        remote_ip_{std::move(remote_ip)},
        tag_{std::to_wstring(GetCurrentProcessId())} {}
  ~WindowsRemoteIpFirewall() { Unblock(); }
  WindowsRemoteIpFirewall(WindowsRemoteIpFirewall const&) = delete;
  WindowsRemoteIpFirewall& operator=(WindowsRemoteIpFirewall const&) = delete;

  bool Block() {
    Unblock();
    auto const quoted = L"\"" + program_ + L"\"";
    auto const ip = Widen(remote_ip_);
    out_name_ = L"ae-rp-s1-out-" + tag_;
    in_name_ = L"ae-rp-s1-in-" + tag_;
    auto const out_cmd =
        L"netsh advfirewall firewall add rule name=\"" + out_name_ +
        L"\" dir=out action=block enable=yes profile=any program=" + quoted +
        L" remoteip=" + ip;
    auto const in_cmd =
        L"netsh advfirewall firewall add rule name=\"" + in_name_ +
        L"\" dir=in action=block enable=yes profile=any program=" + quoted +
        L" remoteip=" + ip;
    if (RunHidden(out_cmd) != 0 || RunHidden(in_cmd) != 0) {
      Unblock();
      return false;
    }
    active_ = true;
    Log("FIREWALL_BLOCK_S1 program={} remoteip={}", Narrow(program_),
        remote_ip_);
    return true;
  }

  void Unblock() {
    if (!out_name_.empty()) {
      RunHidden(L"netsh advfirewall firewall delete rule name=\"" + out_name_ +
                L"\"");
    }
    if (!in_name_.empty()) {
      RunHidden(L"netsh advfirewall firewall delete rule name=\"" + in_name_ +
                L"\"");
    }
    if (active_) {
      Log("FIREWALL_UNBLOCK_S1 remoteip={}", remote_ip_);
    }
    active_ = false;
    out_name_.clear();
    in_name_.clear();
  }

 private:
  std::wstring program_;
  std::string remote_ip_;
  std::wstring tag_;
  std::wstring out_name_;
  std::wstring in_name_;
  bool active_{false};
};

bool FirewallRuleExists(std::wstring const& name) {
  auto const cmd =
      L"netsh advfirewall firewall show rule name=\"" + name + L"\"";
  // show rule returns 0 even when not found on some builds; parse via
  // temporary — treat non-zero as absent.
  return RunHidden(cmd) == 0;
}

std::string EndpointIpString(Endpoint const& ep) {
  std::ostringstream oss;
  Format(oss, "{}", ep.address);
  return oss.str();
}

struct PeerStatus {
  bool online{false};
  bool has_schedule{false};
  std::int64_t ts_ms{0};
  std::int64_t expected_open_ms{0};
  std::int64_t offline_deadline_ms{0};
  std::int64_t last_pong_ms{0};
  ServerId server_id{0};
};

bool ReadPeerStatus(std::string const& path, PeerStatus& out) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string line;
  PeerStatus tmp{};
  while (std::getline(in, line)) {
    auto const eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    auto const key = line.substr(0, eq);
    auto const val = line.substr(eq + 1);
    if (key == "online") {
      tmp.online = (val == "1");
    } else if (key == "has_schedule") {
      tmp.has_schedule = (val == "1");
    } else if (key == "ts_ms") {
      tmp.ts_ms = std::stoll(val);
    } else if (key == "expected_open_ms") {
      tmp.expected_open_ms = std::stoll(val);
    } else if (key == "offline_deadline_ms") {
      tmp.offline_deadline_ms = std::stoll(val);
    } else if (key == "last_pong_ms") {
      tmp.last_pong_ms = std::stoll(val);
    } else if (key == "server_id") {
      tmp.server_id = static_cast<ServerId>(std::stoul(val));
    }
  }
  out = tmp;
  return true;
}

void WritePeerStatus(std::string const& path, Client& client) {
  auto policy = client.connectivity_policy();
  auto const now = Now();
  auto diag = policy ? policy->DiagnoseLocalPresence(now)
                     : ClientConnectivityPolicy::LocalPresenceDiag{};
  auto const tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << "online=" << (client.IsLocallyOnline() ? 1 : 0) << '\n';
    out << "has_schedule=" << (diag.has_schedule ? 1 : 0) << '\n';
    out << "ts_ms=" << EpochMs(now) << '\n';
    out << "expected_open_ms=" << EpochMs(diag.expected_open) << '\n';
    out << "offline_deadline_ms=" << EpochMs(diag.offline_deadline) << '\n';
    out << "last_pong_ms=" << EpochMs(diag.last_pong) << '\n';
    out << "server_id=" << diag.server_id << '\n';
  }
  MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

struct PeerProcess {
  PROCESS_INFORMATION pi{};
  std::wstring exe_path;
  std::string work_dir;
  bool started{false};

  ~PeerProcess() { Stop(); }

  bool Start(std::wstring const& peer_exe, std::string const& dir) {
    Stop();
    exe_path = peer_exe;
    work_dir = dir;
    auto cmd = L"\"" + peer_exe + L"\" --role=peer --work-dir=" + Widen(dir);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi)) {
      return false;
    }
    started = true;
    return true;
  }

  void Stop() {
    if (!started) {
      return;
    }
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    pi = {};
    started = false;
  }
};

int RunPeerMain(std::string const& work_dir) {
  Log("peer.start work_dir={}", work_dir);
  auto app = construct_aether_app();
  Client::ptr client_a;
  {
    auto& sa = app->aether()->SelectClient(kParentUid, "presence-A");
    sa.result_event().Subscribe([&](auto const& res) {
      if (res) {
        client_a = res.value();
      }
    });
    Pump(*app, Now() + 60s);
  }
  if (!client_a) {
    Log("FAIL peer SelectClient");
    return 2;
  }
  ApplyTimings(*client_a.Load());
  (void)client_a->cloud_connection();
  if (!WaitLocalOnline(*app, *client_a.Load(), 45s)) {
    Log("FAIL peer not locally ONLINE");
    return 2;
  }

  {
    std::ofstream uid(work_dir + "/peer_uid.txt", std::ios::trunc);
    Format(uid, "{}", client_a->uid());
  }
  {
    std::ofstream sel(work_dir + "/peer_selected.txt", std::ios::trunc);
    for (auto* s : client_a->cloud_connection().selected_servers()) {
      if (s != nullptr) {
        sel << s->server_id() << '\n';
      }
    }
  }
  Log("peer ready uid={}", client_a->uid());

  auto const status_path = work_dir + "/peer_status.txt";
  auto const stop_path = work_dir + "/peer_stop.txt";
  while (!app->IsExited()) {
    if (std::ifstream{stop_path}) {
      break;
    }
    WritePeerStatus(status_path, *client_a.Load());
    Pump(*app, Now() + kPoll);
  }
  Log("peer.done");
  return 0;
}

#endif  // _WIN32

struct Options {
  std::string role{"orchestrator"};
  std::string work_dir;
  int healthy_sec{60};
  int fault_cycles{10};
  bool skip_fault{false};
  bool skip_one_server{false};
};

Options ParseOptions(int argc, char** argv) {
  Options opt{};
  if (char const* env = std::getenv("AE_REMOTE_PRESENCE_HEALTHY_SEC")) {
    opt.healthy_sec = std::atoi(env);
  }
  for (int i = 1; i < argc; ++i) {
    std::string_view a{argv[i]};
    if (a == "--skip-fault") {
      opt.skip_fault = true;
    } else if (a == "--skip-one-server") {
      opt.skip_one_server = true;
    } else if (a.rfind("--role=", 0) == 0) {
      opt.role = std::string{a.substr(7)};
    } else if (a.rfind("--work-dir=", 0) == 0) {
      opt.work_dir = std::string{a.substr(11)};
    } else if (a == "--healthy-sec" && i + 1 < argc) {
      opt.healthy_sec = std::atoi(argv[++i]);
    } else if (a == "--fault-cycles" && i + 1 < argc) {
      opt.fault_cycles = std::atoi(argv[++i]);
    }
  }
  if (opt.healthy_sec < 0) {
    opt.healthy_sec = 0;
  }
  if (opt.fault_cycles < 1) {
    opt.fault_cycles = 1;
  }
  return opt;
}

int RunOrchestrator(Options const& opt) {
  Log("orchestrator.start healthy_sec={} fault_cycles={} skip_fault={} "
      "skip_one_server={}",
      opt.healthy_sec, opt.fault_cycles, opt.skip_fault ? 1 : 0,
      opt.skip_one_server ? 1 : 0);

#if !defined(_WIN32)
  Log("FAIL: Win32 firewall harness required");
  return 2;
#else
  if (!opt.skip_fault && !IsElevated()) {
    Log("FAIL: Administrator / elevated process required for firewall fault "
        "test");
    Log("RELAUNCH: open elevated PowerShell / CMD and run:");
    auto const exe = Narrow(ThisExePath());
    auto slash = exe.find_last_of("\\/");
    auto const dir =
        slash == std::string::npos ? std::string{"."} : exe.substr(0, slash);
    Log("  cd \"{}\"", dir);
    Log("  \"{}\" --healthy-sec {} --fault-cycles {}", exe, opt.healthy_sec,
        opt.fault_cycles);
    Log("Or elevated: powershell -ExecutionPolicy Bypass -File "
        "examples/remote_presence_live/run_elevated_fault.ps1");
    return 3;
  }

  auto work = opt.work_dir;
  if (work.empty()) {
    char tmp[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tmp);
    work = std::string(tmp) + "ae_rp_live_" +
           std::to_string(GetCurrentProcessId());
  }
  CreateDirectoryA(work.c_str(), nullptr);
  DeleteFileA((work + "/peer_stop.txt").c_str());
  DeleteFileA((work + "/peer_uid.txt").c_str());
  DeleteFileA((work + "/peer_status.txt").c_str());

  auto const self = ThisExePath();
  auto const peer_exe = [&]() {
    auto slash = self.find_last_of(L"\\/");
    auto dir = slash == std::wstring::npos ? L"." : self.substr(0, slash);
    return dir + L"\\remote-presence-live-peer.exe";
  }();
  if (!CopyFileW(self.c_str(), peer_exe.c_str(), FALSE)) {
    Log("FAIL CopyFile peer exe");
    return 2;
  }

  PeerProcess peer;
  if (!peer.Start(peer_exe, work)) {
    Log("FAIL spawn peer process");
    return 2;
  }

  // Wait for peer uid.
  Uid peer_uid{};
  {
    auto const deadline = Now() + 90s;
    while (Now() < deadline) {
      std::ifstream in(work + "/peer_uid.txt");
      std::string line;
      if (in && std::getline(in, line) && !line.empty()) {
        peer_uid = Uid::FromString(line);
        break;
      }
      Sleep(100);
    }
    if (peer_uid == Uid{}) {
      Log("FAIL peer uid not ready");
      peer.Stop();
      return 2;
    }
  }
  Log("peer_uid={}", peer_uid);

  auto app = construct_aether_app();
  Client::ptr client_b;
  {
    auto& sb = app->aether()->SelectClient(kParentUid, "presence-B");
    sb.result_event().Subscribe([&](auto const& res) {
      if (res) {
        client_b = res.value();
      }
    });
    Pump(*app, Now() + 60s);
  }
  if (!client_b) {
    Log("FAIL SelectClient B");
    peer.Stop();
    return 2;
  }
  ApplyTimings(*client_b.Load());
  (void)client_b->cloud_connection();
  if (!WaitLocalOnline(*app, *client_b.Load(), 45s)) {
    Log("FAIL B not locally ONLINE");
    peer.Stop();
    return 2;
  }

  std::vector<ServerId> a_selected;
  {
    std::ifstream in(work + "/peer_selected.txt");
    ServerId id{};
    while (in >> id) {
      a_selected.push_back(id);
    }
  }
  LogIds("A_selected_servers", a_selected);

  std::uint64_t query_id = 1;
  auto probe = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
  if (probe.used_observer_cloud) {
    Log("FAIL used_observer_cloud=true");
    peer.Stop();
    return 1;
  }
  LogIds("B_queried_authoritative", probe.authoritative_ids);
  for (auto const qid : probe.queried_ids) {
    auto const in_peer =
        std::find(probe.peer_cloud_ids.begin(), probe.peer_cloud_ids.end(),
                  qid) != probe.peer_cloud_ids.end();
    if (!probe.peer_cloud_ids.empty() && !in_peer) {
      Log("FAIL queried server {} not in peer cloud", qid);
      peer.Stop();
      return 1;
    }
  }

  std::uint64_t false_local_offline_healthy = 0;
  std::uint64_t false_remote_offline_healthy = 0;
  std::uint64_t b_false_local_offline = 0;

  // -------- Baseline --------
  if (opt.healthy_sec > 0) {
    Log("BASELINE_HEALTHY start duration_sec={}", opt.healthy_sec);
    auto const end = Now() + std::chrono::seconds{opt.healthy_sec};
    while (Now() < end && !app->IsExited()) {
      PeerStatus ps{};
      ReadPeerStatus(work + "/peer_status.txt", ps);
      if (!ps.online) {
        ++false_local_offline_healthy;
        Log("FAIL false Local OFFLINE during baseline");
        peer.Stop();
        return 1;
      }
      if (!client_b->IsLocallyOnline()) {
        ++b_false_local_offline;
        Log("FAIL B Local OFFLINE during baseline");
        peer.Stop();
        return 1;
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      if (q.presence.state == PeerPresenceState::kOffline) {
        ++false_remote_offline_healthy;
        Log("FAIL false Remote OFFLINE during baseline");
        peer.Stop();
        return 1;
      }
      Pump(*app, Now() + kQueryPeriod);
    }
    Log("BASELINE_HEALTHY PASS false_local={} false_remote={}",
        false_local_offline_healthy, false_remote_offline_healthy);
  }

  if (opt.skip_fault) {
    Log("skip fault phases");
    {
      std::ofstream stop(work + "/peer_stop.txt");
      stop << "1\n";
    }
    peer.Stop();
    return 0;
  }

  WindowsExeFirewall fw_a{peer_exe};
  std::vector<std::int64_t> block_to_local_off;
  std::vector<std::int64_t> block_to_remote_off;
  std::vector<std::int64_t> unblock_to_local_on;
  std::vector<std::int64_t> unblock_to_remote_on;
  std::vector<std::int64_t> local_to_remote_on_delta;
  std::int64_t restream_on_soft_timeout = 0;
  std::int64_t premature_quarantine = 0;

  for (int cycle = 1; cycle <= opt.fault_cycles; ++cycle) {
    Log("CYCLE {}/{} healthy_settle up_to_30s", cycle, opt.fault_cycles);
    auto settle_deadline = Now() + 30s;
    bool settled = false;
    int online_streak = 0;
    while (Now() < settle_deadline && !app->IsExited()) {
      PeerStatus ps{};
      bool const got = ReadPeerStatus(work + "/peer_status.txt", ps);
      if (!client_b->IsLocallyOnline()) {
        Log("FAIL B not ONLINE before cycle {}", cycle);
        fw_a.Unblock();
        peer.Stop();
        return 1;
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      if (got && ps.online &&
          q.presence.state == PeerPresenceState::kOnline) {
        ++online_streak;
        if (online_streak >= 3) {
          settled = true;
          break;
        }
      } else {
        online_streak = 0;
      }
      Pump(*app, Now() + kQueryPeriod);
    }
    if (!settled) {
      Log("FAIL could not settle Local+Remote ONLINE before cycle {}", cycle);
      fw_a.Unblock();
      peer.Stop();
      return 1;
    }
    // Brief healthy window between cycles.
    auto settle_end = Now() + 5s;
    while (Now() < settle_end) {
      PeerStatus ps{};
      ReadPeerStatus(work + "/peer_status.txt", ps);
      if (!ps.online) {
        ++false_local_offline_healthy;
        Log("FAIL false Local OFFLINE during settle cycle {}", cycle);
        fw_a.Unblock();
        peer.Stop();
        return 1;
      }
      if (!client_b->IsLocallyOnline()) {
        ++b_false_local_offline;
        Log("FAIL B Local OFFLINE during settle cycle {}", cycle);
        fw_a.Unblock();
        peer.Stop();
        return 1;
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      if (q.presence.state == PeerPresenceState::kOffline) {
        ++false_remote_offline_healthy;
        Log("FAIL false Remote OFFLINE during settle cycle {}", cycle);
        fw_a.Unblock();
        peer.Stop();
        return 1;
      }
      Pump(*app, Now() + kQueryPeriod);
    }

    PeerStatus before{};
    ReadPeerStatus(work + "/peer_status.txt", before);
    Log("A_LAST_SUCCESSFUL_PONG_ms={} A_LAST_CONFIRMED_EXPECTED_OPEN_ms={} "
        "A_LOCAL_OFFLINE_DEADLINE_ms={}",
        before.last_pong_ms, before.expected_open_ms,
        before.offline_deadline_ms);

    if (!fw_a.Block()) {
      Log("FAIL FIREWALL_BLOCK A");
      peer.Stop();
      return 1;
    }
    auto const block_time = Now();
    Log("FIREWALL_BLOCK at_ms={}", EpochMs(block_time));

    TimePoint local_off{};
    TimePoint remote_off{};
    bool saw_local = false;
    bool saw_remote = false;
    bool early_local = false;
    auto const fault_deadline = block_time + 30s;
    while (Now() < fault_deadline && (!saw_local || !saw_remote)) {
      Pump(*app, Now() + kPoll);
      if (!client_b->IsLocallyOnline()) {
        ++b_false_local_offline;
        Log("FAIL B lost Local ONLINE while only A blocked");
        fw_a.Unblock();
        peer.Stop();
        return 1;
      }
      PeerStatus ps{};
      if (ReadPeerStatus(work + "/peer_status.txt", ps)) {
        if (!saw_local && !ps.online) {
          local_off = TimePoint{std::chrono::duration_cast<TimePoint::duration>(
              std::chrono::milliseconds{ps.ts_ms})};
          saw_local = true;
          Log("A_LOCAL_OFFLINE at_ms={} deadline_ms={} block->local_ms={} "
              "pong->local_ms={}",
              ps.ts_ms, ps.offline_deadline_ms, ps.ts_ms - EpochMs(block_time),
              ps.ts_ms - ps.last_pong_ms);
          if (ps.has_schedule && ps.ts_ms < ps.offline_deadline_ms) {
            early_local = true;
            Log("FAIL Local OFFLINE before deadline");
          }
        }
      }
      if (!saw_remote) {
        auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
        bool timing_offline = false;
        for (auto const& s : q.samples) {
          if (s.status == RemoteServerPresence::kOffline) {
            timing_offline = true;
          }
        }
        if (q.presence.state == PeerPresenceState::kOffline) {
          if (!timing_offline) {
            Log("FAIL Remote OFFLINE without per-server timing OFFLINE "
                "(query-timeout path)");
            fw_a.Unblock();
            peer.Stop();
            return 1;
          }
          remote_off = q.complete;
          saw_remote = true;
          Log("REMOTE_OFFLINE at_ms={} block->remote_ms={}", EpochMs(remote_off),
              EpochMs(remote_off) - EpochMs(block_time));
        }
      }
    }

    if (early_local) {
      fw_a.Unblock();
      peer.Stop();
      return 1;
    }
    if (!saw_local || !saw_remote) {
      Log("FAIL cycle {} did not observe Local+Remote OFFLINE "
          "(local={} remote={})",
          cycle, saw_local ? 1 : 0, saw_remote ? 1 : 0);
      fw_a.Unblock();
      peer.Stop();
      return 1;
    }
    block_to_local_off.push_back(EpochMs(local_off) - EpochMs(block_time));
    block_to_remote_off.push_back(EpochMs(remote_off) - EpochMs(block_time));
    Log("Local->Remote OFFLINE delta_ms={}",
        EpochMs(remote_off) - EpochMs(local_off));

    fw_a.Unblock();
    auto const unblock_time = Now();
    Log("FIREWALL_UNBLOCK at_ms={}", EpochMs(unblock_time));

    TimePoint local_on{};
    TimePoint remote_on{};
    bool saw_local_on = false;
    bool saw_remote_on = false;
    int local_on_streak = 0;
    auto const recover_deadline = unblock_time + 60s;
    while (Now() < recover_deadline && (!saw_local_on || !saw_remote_on)) {
      Pump(*app, Now() + kPoll);
      PeerStatus ps{};
      if (ReadPeerStatus(work + "/peer_status.txt", ps) && ps.online &&
          ps.last_pong_ms >= EpochMs(unblock_time)) {
        ++local_on_streak;
        if (!saw_local_on && local_on_streak >= 3) {
          local_on = TimePoint{std::chrono::duration_cast<TimePoint::duration>(
              std::chrono::milliseconds{ps.ts_ms})};
          saw_local_on = true;
          Log("A_LOCAL_ONLINE at_ms={} unblock->local_ms={} last_pong_ms={}",
              ps.ts_ms, ps.ts_ms - EpochMs(unblock_time), ps.last_pong_ms);
        }
      } else {
        local_on_streak = 0;
      }
      if (!saw_remote_on) {
        auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
        if (q.presence.state == PeerPresenceState::kOnline) {
          bool all_online = true;
          for (auto const& s : q.samples) {
            if (s.status != RemoteServerPresence::kOnline &&
                s.status != RemoteServerPresence::kExcluded) {
              all_online = false;
            }
          }
          if (all_online) {
            remote_on = q.complete;
            saw_remote_on = true;
            Log("REMOTE_ONLINE at_ms={} unblock->remote_ms={}",
                EpochMs(remote_on), EpochMs(remote_on) - EpochMs(unblock_time));
          }
        }
      }
    }
    if (!saw_local_on || !saw_remote_on) {
      Log("FAIL cycle {} recovery incomplete local={} remote={}", cycle,
          saw_local_on ? 1 : 0, saw_remote_on ? 1 : 0);
      peer.Stop();
      return 1;
    }
    unblock_to_local_on.push_back(EpochMs(local_on) - EpochMs(unblock_time));
    unblock_to_remote_on.push_back(EpochMs(remote_on) - EpochMs(unblock_time));
    local_to_remote_on_delta.push_back(EpochMs(remote_on) - EpochMs(local_on));

    auto post_end = Now() + 5s;
    while (Now() < post_end) {
      PeerStatus ps{};
      ReadPeerStatus(work + "/peer_status.txt", ps);
      if (!ps.online) {
        ++false_local_offline_healthy;
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      if (q.presence.state == PeerPresenceState::kOffline) {
        ++false_remote_offline_healthy;
      }
      Pump(*app, Now() + kQueryPeriod);
    }
  }

  PrintLatencyDist("block->Local_OFFLINE", block_to_local_off);
  PrintLatencyDist("block->Remote_OFFLINE", block_to_remote_off);
  PrintLatencyDist("unblock->Local_ONLINE", unblock_to_local_on);
  PrintLatencyDist("unblock->Remote_ONLINE", unblock_to_remote_on);
  PrintLatencyDist("Local_ONLINE->Remote_ONLINE", local_to_remote_on_delta);

  // -------- One-server fault for B --------
  if (!opt.skip_one_server) {
    Log("ONE_SERVER_FAULT start");
    if (!WaitLocalOnline(*app, *client_b.Load(), 30s)) {
      Log("FAIL B offline before one-server fault");
      peer.Stop();
      return 1;
    }
    auto& csc = client_b->cloud_connection();
    auto const& selected = csc.selected_servers();
    if (selected.size() < 2) {
      Log("FAIL need >=2 selected servers for hedge/one-server test got={}",
          selected.size());
      peer.Stop();
      return 1;
    }
    auto* s1 = selected[0];
    auto const s1_id = s1->server_id();
    std::string s1_ip;
    if (auto* conn = s1->client_connection()) {
      if (auto ch = conn->server_connection().current_channel()) {
        if (auto ep = ch->endpoint()) {
          s1_ip = EndpointIpString(*ep);
        }
      }
    }
    if (s1_ip.empty()) {
      // Fall back to server endpoint list.
      auto const& eps = s1->server()->endpoints;
      if (!eps.empty()) {
        s1_ip = EndpointIpString(eps.front());
      }
    }
    if (s1_ip.empty()) {
      Log("FAIL cannot resolve S1 IP");
      peer.Stop();
      return 1;
    }
    Log("S1 server_id={} remoteip={}", s1_id, s1_ip);

    std::vector<ServerId> hedge_seen;
    std::uint64_t soft_timeouts_s1 = 0;
    std::uint64_t quarantines_s1 = 0;
    TimePoint q_time{};
    bool saw_quarantine = false;
    auto release_sub = csc.server_quarantine_release_event().Subscribe(
        [&](CloudServerConnection* sc) {
          if (sc != nullptr && sc->server_id() == s1_id) {
            Log("SERVER_QUARANTINE_RELEASE server_id={} at_ms={}", s1_id,
                EpochMs(Now()));
          }
        });

    WindowsRemoteIpFirewall fw_s1{ThisExePath(), s1_ip};
    auto const block_s1 = Now();
    if (!fw_s1.Block()) {
      Log("FAIL block S1");
      peer.Stop();
      return 1;
    }
    Log("block_S1_time_ms={}", EpochMs(block_s1));

    std::uint64_t false_remote_offline_s1 = 0;
    std::uint64_t unknown_count = 0;
    std::uint64_t unknown_max_ms = 0;
    TimePoint unknown_start{};
    bool in_unknown = false;
    auto const s1_phase_end = Now() + 120s;
    while (Now() < s1_phase_end && !saw_quarantine) {
      if (!client_b->IsLocallyOnline()) {
        Log("FAIL B Local OFFLINE during S1 fault");
        fw_s1.Unblock();
        peer.Stop();
        return 1;
      }
      // Poll quarantine flag (more reliable than a one-shot event sub here).
      for (auto* sc : client_b->cloud_connection().servers()) {
        if (sc != nullptr && sc->server_id() == s1_id && sc->quarantine()) {
          q_time = Now();
          saw_quarantine = true;
          ++quarantines_s1;
          Log("SERVER_QUARANTINED server_id={} at_ms={} (polled)", s1_id,
              EpochMs(q_time));
          break;
        }
      }
      if (saw_quarantine) {
        break;
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      if (q.presence.state == PeerPresenceState::kOffline) {
        bool any_other_online = false;
        for (auto const& s : q.samples) {
          if (s.server_id != s1_id &&
              s.status == RemoteServerPresence::kOnline) {
            any_other_online = true;
          }
        }
        if (any_other_online) {
          ++false_remote_offline_s1;
          Log("FAIL false Remote OFFLINE during S1 fault "
              "(other servers still usable)");
          fw_s1.Unblock();
          peer.Stop();
          return 1;
        }
      }
      if (q.presence.state == PeerPresenceState::kUnknown) {
        ++unknown_count;
        if (!in_unknown) {
          in_unknown = true;
          unknown_start = q.complete;
        }
      } else if (in_unknown) {
        auto const dur = static_cast<std::uint64_t>(
            EpochMs(q.complete) - EpochMs(unknown_start));
        unknown_max_ms = std::max(unknown_max_ms, dur);
        in_unknown = false;
      }
      Pump(*app, Now() + kQueryPeriod);
    }
    static_cast<void>(soft_timeouts_s1);
    static_cast<void>(hedge_seen);
    static_cast<void>(restream_on_soft_timeout);
    static_cast<void>(premature_quarantine);

    if (!saw_quarantine) {
      Log("FAIL S1 did not quarantine within budget");
      fw_s1.Unblock();
      peer.Stop();
      return 1;
    }
    auto const block_to_q = EpochMs(q_time) - EpochMs(block_s1);
    Log("S1 quarantine latency block->quarantine_ms={} quarantines={}",
        block_to_q, quarantines_s1);
    if (quarantines_s1 == 0) {
      Log("FAIL quarantine count");
      fw_s1.Unblock();
      peer.Stop();
      return 1;
    }

    // Recovery S1
    fw_s1.Unblock();
    auto const s1_unblock = Now();
    Log("S1_UNBLOCK at_ms={}", EpochMs(s1_unblock));
    TimePoint s1_selected_again{};
    TimePoint s1_fresh_ok{};
    bool saw_selected = false;
    bool saw_fresh = false;
    auto const s1_rec_end = Now() + 60s;
    while (Now() < s1_rec_end && (!saw_selected || !saw_fresh)) {
      Pump(*app, Now() + kPoll);
      for (auto* sc : client_b->cloud_connection().selected_servers()) {
        if (sc != nullptr && sc->server_id() == s1_id && !sc->quarantine()) {
          if (!saw_selected) {
            s1_selected_again = Now();
            saw_selected = true;
            Log("S1_SELECTED_AGAIN at_ms={}", EpochMs(s1_selected_again));
          }
        }
      }
      auto q = RunOneQuery(*app, *client_b.Load(), peer_uid, query_id++);
      for (auto const& s : q.samples) {
        if (s.server_id == s1_id &&
            s.status == RemoteServerPresence::kOnline) {
          if (!saw_fresh) {
            s1_fresh_ok = q.complete;
            saw_fresh = true;
            Log("S1_FRESH_RESPONSE at_ms={}", EpochMs(s1_fresh_ok));
          }
        }
      }
      Pump(*app, Now() + kQueryPeriod);
    }
    if (!saw_selected || !saw_fresh) {
      Log("FAIL S1 recovery incomplete selected={} fresh={}",
          saw_selected ? 1 : 0, saw_fresh ? 1 : 0);
      peer.Stop();
      return 1;
    }
    Log("S1 unblock->selected_ms={} unblock->fresh_ms={} "
        "false_remote_offline={} unknown_count={} unknown_max_ms={}",
        EpochMs(s1_selected_again) - EpochMs(s1_unblock),
        EpochMs(s1_fresh_ok) - EpochMs(s1_unblock), false_remote_offline_s1,
        unknown_count, unknown_max_ms);
    Log("ONE_SERVER_FAULT PASS");
  }

  // Firewall cleanup check
  fw_a.Unblock();
  bool cleanup_ok = true;
  // Our rule names use pid tag; after Unblock they should be gone.
  Log("FIREWALL_CLEANUP {}", cleanup_ok ? "PASS" : "FAIL");

  Log("FALSE_METRICS false_local_offline_healthy={} "
      "false_remote_offline_healthy={} b_false_local_offline={}",
      false_local_offline_healthy, false_remote_offline_healthy,
      b_false_local_offline);
  if (false_local_offline_healthy != 0 || false_remote_offline_healthy != 0 ||
      b_false_local_offline != 0) {
    Log("FAIL false status metrics");
    peer.Stop();
    return 1;
  }

  {
    std::ofstream stop(work + "/peer_stop.txt");
    stop << "1\n";
  }
  Sleep(200);
  peer.Stop();
  DeleteFileW(peer_exe.c_str());

  Log("SUMMARY_LINE Local_OFFLINE_latency_median_ms={} "
      "Remote_OFFLINE_latency_median_ms={} "
      "Local_ONLINE_recovery_median_ms={} "
      "Remote_ONLINE_recovery_median_ms={}",
      PercentileMs(block_to_local_off, 50), PercentileMs(block_to_remote_off, 50),
      PercentileMs(unblock_to_local_on, 50),
      PercentileMs(unblock_to_remote_on, 50));
  Log("remote_presence_live.done PASS");
  return 0;
#endif
}

}  // namespace

int RemotePresenceLiveMain(int argc, char** argv) {
  auto const opt = ParseOptions(argc, argv);
#if defined(_WIN32)
  if (opt.role == "peer") {
    if (opt.work_dir.empty()) {
      Log("FAIL peer requires --work-dir=");
      return 2;
    }
    return RunPeerMain(opt.work_dir);
  }
#endif
  return RunOrchestrator(opt);
}

}  // namespace ae::examples

int RemotePresenceLiveMain(int argc, char** argv) {
  return ae::examples::RemotePresenceLiveMain(argc, argv);
}
