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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include <unity.h>

#include "aether/adapters/ethernet.h"
#include "aether/aether_app.h"
#include "aether/all.h"
#include "aether/client.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/global_ids.h"
#include "aether/types/uid.h"

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace ae::test_local_presence_firewall {

using namespace std::chrono_literals;

constexpr auto kInterval = 1s;
constexpr auto kWindow = 1s;
constexpr auto kPoll = 10ms;

static constexpr auto kParentUid =
    Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

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
    out_name_ = L"ae-lp-fw-out-" + tag_;
    in_name_ = L"ae-lp-fw-in-" + tag_;
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

 private:
  std::wstring exe_path_;
  std::wstring tag_;
  std::wstring out_name_;
  std::wstring in_name_;
  bool active_{false};
};

#endif

std::unique_ptr<AetherApp> MakeApp() {
  return AetherApp::Construct(AetherAppContext{}.AddAdapterFactory(
      [](AetherAppContext const& context) {
        return EthernetAdapter::ptr::Create(
            CreateWith{context.domain()}.with_id(GlobalId::kEthernetAdapter),
            context.aether(), context.poller(), context.dns_resolver());
      }));
}

void Pump(AetherApp& app, TimePoint until) {
  while (!app.IsExited() && Now() < until) {
    auto const next = app.Update(Now());
    auto const poll_at = Now() + kPoll;
    app.WaitUntil(next < poll_at ? next : poll_at);
  }
}

TimePoint EarliestConfirmedClose(Client& client) {
  auto close = TimePoint::max();
  auto policy = client.connectivity_policy();
  if (!policy) {
    return close;
  }
  for (auto* server : client.cloud_connection().selected_servers()) {
    if (server == nullptr) {
      continue;
    }
    auto const* state = policy->FindServerPresence(server->server_id());
    if (state != nullptr && state->has_confirmed_schedule) {
      close = std::min(close, state->confirmed_window_close_local);
    }
  }
  return close;
}

void ApplyOneSecondTimings(Client& client) {
  auto policy = client.connectivity_policy();
  TEST_ASSERT_TRUE(static_cast<bool>(policy));
  policy->ResetRxTimings();
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

void test_WindowsFirewallOfflineAndRecovery() {
#if !defined(_WIN32)
  TEST_IGNORE_MESSAGE("Windows Firewall test runs on Win32 only");
#else
  auto app = MakeApp();
  TEST_ASSERT_NOT_NULL(app.get());

  Client::ptr client;
  auto& select = app->aether()->SelectClient(kParentUid, "presence-fw");
  select.result_event().Subscribe([&](auto const& res) {
    if (res) {
      client = res.value();
    }
  });
  Pump(*app, Now() + 45s);
  if (!client) {
    TEST_IGNORE_MESSAGE("SelectClient did not finish (no cloud / network)");
  }

  ApplyOneSecondTimings(*client.Load());
  (void)client->cloud_connection();

  auto const online_deadline = Now() + 30s;
  while (Now() < online_deadline && !app->IsExited()) {
    Pump(*app, Now() + kPoll);
    if (client->IsLocallyOnline()) {
      break;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(client->IsLocallyOnline(),
                           "did not become ONLINE before firewall");
  Pump(*app, Now() + 1500ms);
  TEST_ASSERT_TRUE(client->IsLocallyOnline());

  auto const close = EarliestConfirmedClose(*client.Load());
  TEST_ASSERT_TRUE_MESSAGE(close != TimePoint::max(),
                           "no confirmed receive window");

  WindowsExeFirewall fw{ThisExePath()};
  auto const fault_time = Now();
  TEST_ASSERT_TRUE_MESSAGE(
      fw.Block(),
      "netsh advfirewall failed (run the test as Administrator)");

  bool early_offline = false;
  TimePoint detected_offline{};
  auto const detect_deadline = close + 3s;
  while (Now() < detect_deadline && !app->IsExited()) {
    Pump(*app, Now() + kPoll);
    auto const online = client->IsLocallyOnline();
    auto const now = Now();
    if (now <= close) {
      if (!online) {
        early_offline = true;
        detected_offline = now;
        break;
      }
    } else if (!online) {
      detected_offline = now;
      break;
    }
  }

  auto const close_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(close - fault_time)
          .count();
  auto const detected_ms =
      detected_offline.time_since_epoch().count() == 0
          ? -1
          : std::chrono::duration_cast<std::chrono::milliseconds>(
                detected_offline - fault_time)
                .count();
  std::printf(
      "FIREWALL fault interval_ms=1000 rx_window_ms=1000 "
      "fault_to_window_close_ms=%lld detected_offline_after_fault_ms=%lld "
      "early_offline=%s\n",
      static_cast<long long>(close_ms), static_cast<long long>(detected_ms),
      early_offline ? "YES" : "NO");
  if (FILE* log = std::fopen("firewall_result.txt", "w")) {
    std::fprintf(
        log,
        "interval_ms=1000\nrx_window_ms=1000\n"
        "fault_to_window_close_ms=%lld\ndetected_offline_after_fault_ms=%lld\n"
        "early_offline=%s\n",
        static_cast<long long>(close_ms), static_cast<long long>(detected_ms),
        early_offline ? "YES" : "NO");
    std::fclose(log);
  }

  TEST_ASSERT_FALSE_MESSAGE(early_offline,
                            "OFFLINE appeared before confirmed_window_close");
  TEST_ASSERT_TRUE_MESSAGE(detected_offline.time_since_epoch().count() != 0,
                           "OFFLINE was not detected after firewall block");
  TEST_ASSERT_TRUE(detected_offline > close);

  fw.Unblock();
  auto const recover_from = Now();
  TimePoint recovered{};
  while (Now() < recover_from + 20s && !app->IsExited()) {
    Pump(*app, Now() + kPoll);
    if (client->IsLocallyOnline()) {
      recovered = Now();
      break;
    }
  }
  auto const recovery_ms =
      recovered.time_since_epoch().count() == 0
          ? -1
          : std::chrono::duration_cast<std::chrono::milliseconds>(
                recovered - recover_from)
                .count();
  std::printf("FIREWALL recovery_latency_ms=%lld\n",
              static_cast<long long>(recovery_ms));
  if (FILE* log = std::fopen("firewall_result.txt", "a")) {
    std::fprintf(log, "recovery_latency_ms=%lld\npass=1\n",
                 static_cast<long long>(recovery_ms));
    std::fclose(log);
  }
  TEST_ASSERT_TRUE_MESSAGE(client->IsLocallyOnline(),
                           "did not return ONLINE after firewall unblock");
#endif
}

}  // namespace ae::test_local_presence_firewall

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_local_presence_firewall::test_WindowsFirewallOfflineAndRecovery);
  return UNITY_END();
}
