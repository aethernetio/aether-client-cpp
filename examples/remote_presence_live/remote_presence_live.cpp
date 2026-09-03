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
 * Live Remote Presence harness: client A (peer) + client B (observer).
 *
 * Env / args:
 *   AE_REMOTE_PRESENCE_HEALTHY_SEC  healthy window seconds (default 300)
 *   --healthy-sec N
 *   --skip-fault                   skip firewall fault/recovery phases
 *   --fault-only                   skip healthy statistical window
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
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "aether-miscpp/format/format.h"
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/all.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/cloud_request_execution_policy.h"
#include "aether/cloud_connections/local_presence_schedule.h"
#include "aether/config.h"
#include "aether/remote_presence.h"

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
      CloudRequestExecutionPolicy::FromFactor(99, 1.2, /*retries=*/2,
                                              /*hedge=*/2));
  policy->ConfigureRxTimings(RequestPolicy::All{})
      .ForAllPriorities(RxTimingConf::Every(kInterval).WithWindow(kWindow));
  for (auto* server : client.cloud_connection().selected_servers()) {
    if (server == nullptr) {
      continue;
    }
    policy->ConfigureServerRxTiming(
        server->server_id(),
        RxTimingConf::Every(kInterval).WithWindow(kWindow), 99);
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

struct QueryStats {
  std::uint64_t query_count{0};
  std::uint64_t online_count{0};
  std::uint64_t offline_count{0};
  std::uint64_t unknown_count{0};
  std::uint64_t false_offline_samples{0};
  std::uint64_t false_offline_transitions{0};
  std::uint64_t unknown_max_duration_ms{0};
  PeerPresenceState last{PeerPresenceState::kUnknown};
  TimePoint unknown_started{};
  bool in_unknown{false};
};

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
  Log("query_id={} start_ms={} complete_ms={} aggregate={} "
      "used_observer_cloud={}",
      q.query_id, EpochMs(q.start), EpochMs(q.complete),
      StateName(q.presence.state), q.used_observer_cloud ? 1 : 0);
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

void UpdateStats(QueryStats& stats, QueryResult const& q, bool peer_alive) {
  ++stats.query_count;
  switch (q.presence.state) {
    case PeerPresenceState::kOnline:
      ++stats.online_count;
      break;
    case PeerPresenceState::kOffline:
      ++stats.offline_count;
      if (peer_alive) {
        ++stats.false_offline_samples;
        if (stats.last != PeerPresenceState::kOffline) {
          ++stats.false_offline_transitions;
        }
      }
      break;
    case PeerPresenceState::kUnknown:
      ++stats.unknown_count;
      break;
  }
  if (q.presence.state == PeerPresenceState::kUnknown) {
    if (!stats.in_unknown) {
      stats.in_unknown = true;
      stats.unknown_started = q.complete;
    }
  } else if (stats.in_unknown) {
    auto const dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                         q.complete - stats.unknown_started)
                         .count();
    if (dur > 0 &&
        static_cast<std::uint64_t>(dur) > stats.unknown_max_duration_ms) {
      stats.unknown_max_duration_ms = static_cast<std::uint64_t>(dur);
    }
    stats.in_unknown = false;
  }
  stats.last = q.presence.state;
}

void PrintStats(char const* title, QueryStats const& s) {
  Log("STATS {} query_count={} ONLINE={} OFFLINE={} UNKNOWN={} "
      "false_OFFLINE_samples={} false_OFFLINE_transitions={} "
      "unknown_max_duration_ms={}",
      title, s.query_count, s.online_count, s.offline_count, s.unknown_count,
      s.false_offline_samples, s.false_offline_transitions,
      s.unknown_max_duration_ms);
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
    active_ = false;
  }

  bool active() const { return active_; }

 private:
  std::wstring exe_path_;
  std::wstring tag_;
  std::wstring out_name_;
  std::wstring in_name_;
  bool active_{false};
};
#endif

struct Options {
  int healthy_sec{300};
  bool skip_fault{false};
  bool fault_only{false};
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
    } else if (a == "--fault-only") {
      opt.fault_only = true;
    } else if (a == "--healthy-sec" && i + 1 < argc) {
      opt.healthy_sec = std::atoi(argv[++i]);
    }
  }
  if (opt.healthy_sec < 0) {
    opt.healthy_sec = 0;
  }
  return opt;
}

}  // namespace

int RemotePresenceLiveMain(int argc, char** argv) {
  auto const opt = ParseOptions(argc, argv);
  Log("remote_presence_live.start healthy_sec={} skip_fault={} fault_only={}",
      opt.healthy_sec, opt.skip_fault ? 1 : 0, opt.fault_only ? 1 : 0);

  auto app = construct_aether_app();
  Client::ptr client_a;
  Client::ptr client_b;

  {
    auto& sa = app->aether()->SelectClient(kParentUid, "presence-A");
    sa.result_event().Subscribe([&](auto const& res) {
      if (res) {
        client_a = res.value();
      }
    });
    auto& sb = app->aether()->SelectClient(kParentUid, "presence-B");
    sb.result_event().Subscribe([&](auto const& res) {
      if (res) {
        client_b = res.value();
      }
    });
    Pump(*app, Now() + 60s);
  }

  if (!client_a || !client_b) {
    Log("FAIL SelectClient A/B (no cloud / network) — SKIP live validation");
    return 2;
  }

  Log("clients ready A={} B={}", client_a->uid(), client_b->uid());
  ApplyTimings(*client_a.Load());
  ApplyTimings(*client_b.Load());
  (void)client_a->cloud_connection();
  (void)client_b->cloud_connection();

  if (!WaitLocalOnline(*app, *client_a.Load(), 45s) ||
      !WaitLocalOnline(*app, *client_b.Load(), 45s)) {
    Log("FAIL clients did not become locally ONLINE — SKIP");
    return 2;
  }

  // Authoritative set diagnostics from a single probe query.
  {
    auto probe = RunOneQuery(*app, *client_b.Load(), client_a->uid(), 0);
    Log("AUTHORITATIVE_SET peer_cloud_count={} authoritative_count={} "
        "queried_count={} selected_observer_count={} "
        "AE_CLOUD_MAX_SERVER_CONNECTIONS={}",
        probe.peer_cloud_ids.size(), probe.authoritative_ids.size(),
        probe.queried_ids.size(),
        client_b->cloud_connection().selected_servers().size(),
        AE_CLOUD_MAX_SERVER_CONNECTIONS);
    if (probe.used_observer_cloud) {
      Log("FAIL used_observer_cloud=true (own-cloud fallback must be removed)");
      return 1;
    }
    for (auto const qid : probe.queried_ids) {
      auto const in_peer =
          std::find(probe.peer_cloud_ids.begin(), probe.peer_cloud_ids.end(),
                    qid) != probe.peer_cloud_ids.end();
      if (!probe.peer_cloud_ids.empty() && !in_peer) {
        Log("FAIL queried server {} not in peer cloud", qid);
        return 1;
      }
    }
  }

  std::uint64_t query_id = 1;
  QueryStats healthy{};

  if (!opt.fault_only && opt.healthy_sec > 0) {
    Log("HEALTHY_REMOTE start duration_sec={}", opt.healthy_sec);
    auto const end = Now() + std::chrono::seconds{opt.healthy_sec};
    while (Now() < end && !app->IsExited()) {
      auto q =
          RunOneQuery(*app, *client_b.Load(), client_a->uid(), query_id++);
      UpdateStats(healthy, q, /*peer_alive=*/true);
      if (q.used_observer_cloud) {
        Log("FAIL own-cloud fallback during healthy");
        return 1;
      }
      Pump(*app, Now() + kQueryPeriod);
    }
    PrintStats("healthy_remote", healthy);
    if (healthy.false_offline_samples != 0 ||
        healthy.false_offline_transitions != 0) {
      Log("FAIL healthy remote false OFFLINE");
      return 1;
    }
    Log("HEALTHY_REMOTE PASS");
  }

  if (opt.skip_fault) {
    Log("skip fault/recovery phases");
    return 0;
  }

#if defined(_WIN32)
  if (!IsElevated()) {
    Log("SKIP fault/recovery: Administrator required for firewall block");
    return 0;
  }

  WindowsExeFirewall fw{ThisExePath()};
  // Fault: block this process network — both A and B share the process, so
  // true "A-only" isolation is not possible in-process. Measure Remote
  // OFFLINE under full process block as a transport-dominated bound, and
  // report Local A OFFLINE latency from the same fault.
  Log("FAULT start (process firewall block — A and B share process)");
  auto const fault_time = Now();
  if (!fw.Block()) {
    Log("SKIP fault: netsh advfirewall failed");
    return 0;
  }

  TimePoint local_offline_time{};
  TimePoint remote_offline_time{};
  bool saw_local_offline = false;
  bool saw_remote_offline = false;
  auto const fault_deadline = fault_time + 30s;
  while (Now() < fault_deadline && !app->IsExited()) {
    Pump(*app, Now() + kPoll);
    if (!saw_local_offline && !client_a->IsLocallyOnline()) {
      local_offline_time = Now();
      saw_local_offline = true;
      Log("Local A OFFLINE at_ms={} fault->OFFLINE_ms={}",
          EpochMs(local_offline_time),
          EpochMs(local_offline_time) - EpochMs(fault_time));
    }
    auto q = RunOneQuery(*app, *client_b.Load(), client_a->uid(), query_id++);
    if (!saw_remote_offline &&
        q.presence.state == PeerPresenceState::kOffline) {
      remote_offline_time = q.complete;
      saw_remote_offline = true;
      Log("Remote A OFFLINE at_ms={} fault->OFFLINE_ms={}",
          EpochMs(remote_offline_time),
          EpochMs(remote_offline_time) - EpochMs(fault_time));
      break;
    }
    Pump(*app, Now() + kQueryPeriod);
  }

  if (!saw_remote_offline) {
    Log("NOTE: Remote OFFLINE not observed under process-wide block "
        "(observer B also lost cloud — expected UNKNOWN, not OFFLINE)");
  }

  Log("RECOVERY unblock");
  auto const unblock_time = Now();
  fw.Unblock();

  TimePoint local_online_time{};
  TimePoint remote_online_time{};
  bool saw_local_online = false;
  bool saw_remote_online = false;
  auto const recover_deadline = unblock_time + 60s;
  while (Now() < recover_deadline && !app->IsExited()) {
    Pump(*app, Now() + kPoll);
    if (!saw_local_online && client_a->IsLocallyOnline()) {
      local_online_time = Now();
      saw_local_online = true;
      Log("Local A ONLINE at_ms={} unblock->ONLINE_ms={}",
          EpochMs(local_online_time),
          EpochMs(local_online_time) - EpochMs(unblock_time));
    }
    auto q = RunOneQuery(*app, *client_b.Load(), client_a->uid(), query_id++);
    if (!saw_remote_online &&
        q.presence.state == PeerPresenceState::kOnline) {
      remote_online_time = q.complete;
      saw_remote_online = true;
      Log("Remote A ONLINE at_ms={} unblock->ONLINE_ms={}",
          EpochMs(remote_online_time),
          EpochMs(remote_online_time) - EpochMs(unblock_time));
      break;
    }
    Pump(*app, Now() + kQueryPeriod);
  }

  Log("FAULT_SUMMARY interval=1s offline_detection_timeout=1s "
      "fault_ms={} remote_offline_ms={} fault_to_remote_offline_ms={} "
      "local_offline_ms={} fault_to_local_offline_ms={} "
      "unblock_ms={} local_online_ms={} remote_online_ms={} "
      "unblock_to_local_ms={} unblock_to_remote_ms={}",
      EpochMs(fault_time),
      saw_remote_offline ? EpochMs(remote_offline_time) : -1,
      saw_remote_offline ? (EpochMs(remote_offline_time) - EpochMs(fault_time))
                         : -1,
      saw_local_offline ? EpochMs(local_offline_time) : -1,
      saw_local_offline ? (EpochMs(local_offline_time) - EpochMs(fault_time))
                        : -1,
      EpochMs(unblock_time),
      saw_local_online ? EpochMs(local_online_time) : -1,
      saw_remote_online ? EpochMs(remote_online_time) : -1,
      saw_local_online ? (EpochMs(local_online_time) - EpochMs(unblock_time))
                       : -1,
      saw_remote_online ? (EpochMs(remote_online_time) - EpochMs(unblock_time))
                        : -1);

  // All-servers-unavailable under process block should be UNKNOWN, never
  // Offline solely because the observer lost cloud. Re-check with a short
  // block while capturing aggregate.
  {
    Log("ALL_SERVERS_UNAVAILABLE probe");
    if (!fw.Block()) {
      Log("SKIP all-servers probe");
    } else {
      auto q = RunOneQuery(*app, *client_b.Load(), client_a->uid(), query_id++);
      Log("all_servers_unavailable aggregate={} (expect UNKNOWN, never "
          "OFFLINE-from-own-loss alone)",
          StateName(q.presence.state));
      bool pass = q.presence.state != PeerPresenceState::kOffline ||
                  !q.authoritative_ids.empty();
      // If every usable authoritative server is unreachable, status must be
      // UNKNOWN (usable_count==0 or unresolved), not a fabricated Offline.
      if (q.presence.state == PeerPresenceState::kOffline) {
        bool any_offline_sample = false;
        for (auto const& s : q.samples) {
          if (s.status == RemoteServerPresence::kOffline) {
            any_offline_sample = true;
          }
        }
        pass = any_offline_sample;
      } else {
        pass = q.presence.state == PeerPresenceState::kUnknown;
      }
      Log("ALL_SERVERS_UNAVAILABLE {}", pass ? "PASS" : "FAIL");
      fw.Unblock();
      if (!pass) {
        return 1;
      }
      WaitLocalOnline(*app, *client_a.Load(), 45s);
      WaitLocalOnline(*app, *client_b.Load(), 45s);
    }
  }

  Log("ONE_SERVER_UNAVAILABLE SKIP (requires multi-server isolation of one "
      "peer server from B only — not available in shared-process harness)");
  Log("FIREWALL phases completed (process-wide block)");
#else
  Log("SKIP fault/recovery/firewall: Win32-only in this harness");
#endif

  Log("remote_presence_live.done");
  return 0;
}

}  // namespace ae::examples

int RemotePresenceLiveMain(int argc, char** argv) {
  return ae::examples::RemotePresenceLiveMain(argc, argv);
}
