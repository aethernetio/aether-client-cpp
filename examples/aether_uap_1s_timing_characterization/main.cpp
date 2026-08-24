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

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "client_role.h"
#include "coordinator.h"

namespace {

std::string_view ArgValue(int argc, char** argv, std::string_view key) {
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    if (a == key && i + 1 < argc) {
      return argv[i + 1];
    }
    if (a.size() > key.size() && a.substr(0, key.size()) == key &&
        a[key.size()] == '=') {
      return a.substr(key.size() + 1);
    }
  }
  return {};
}

bool HasFlag(int argc, char** argv, std::string_view key) {
  for (int i = 1; i < argc; ++i) {
    if (key == argv[i]) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ae::test_uap_ping_retry_window;

  auto role = ArgValue(argc, argv, "--role");
  if (role.empty() || role == "coordinator") {
    CharacterizationArgs args;
    args.run_id = std::string{ArgValue(argc, argv, "--run-id")};
    args.artifact_dir = std::string{ArgValue(argc, argv, "--artifact-dir")};
    args.exe_path = std::string{ArgValue(argc, argv, "--exe")};
    auto parent = ArgValue(argc, argv, "--parent-uid");
    if (!parent.empty()) {
      args.parent_uid = std::string{parent};
    }
    auto transport = ArgValue(argc, argv, "--transport");
    if (!transport.empty()) {
      args.transport = std::string{transport};
    }
    auto interval = ArgValue(argc, argv, "--ping-interval-ms");
    if (!interval.empty()) {
      args.ping_interval_ms = std::strtoll(interval.data(), nullptr, 10);
    }
    auto window = ArgValue(argc, argv, "--receive-window-ms");
    if (!window.empty()) {
      args.receive_window_ms = std::strtoll(window.data(), nullptr, 10);
    }
    auto seed = ArgValue(argc, argv, "--seed");
    if (!seed.empty()) {
      args.seed = static_cast<std::uint32_t>(std::strtoul(seed.data(), nullptr, 10));
    }
    auto cycles = ArgValue(argc, argv, "--cycles");
    if (!cycles.empty()) {
      args.logical_cycles = std::atoi(cycles.data());
    }
    auto hard_stop = ArgValue(argc, argv, "--hard-stop-runs");
    if (!hard_stop.empty()) {
      args.hard_stop_runs = std::atoi(hard_stop.data());
    }
    auto graceful = ArgValue(argc, argv, "--graceful-runs");
    if (!graceful.empty()) {
      args.graceful_runs = std::atoi(graceful.data());
    }
    auto main_n = ArgValue(argc, argv, "--window-samples-main");
    if (!main_n.empty()) {
      args.window_samples_main = std::atoi(main_n.data());
    }
    auto extra_n = ArgValue(argc, argv, "--window-samples-extra");
    if (!extra_n.empty()) {
      args.window_samples_extra = std::atoi(extra_n.data());
    }
    auto loss = ArgValue(argc, argv, "--loss-cases");
    if (!loss.empty()) {
      args.loss_cases = std::atoi(loss.data());
    }
    args.quick = HasFlag(argc, argv, "--quick");
    args.skip_long_characterization =
        HasFlag(argc, argv, "--no-long-characterization") || args.quick;
    if (args.quick) {
      if (ArgValue(argc, argv, "--cycles").empty()) {
        args.logical_cycles = 10;
      }
      if (ArgValue(argc, argv, "--hard-stop-runs").empty() &&
          ArgValue(argc, argv, "--hard-stop-cases").empty()) {
        args.hard_stop_runs = 3;
      }
      auto hard_cases = ArgValue(argc, argv, "--hard-stop-cases");
      if (!hard_cases.empty()) {
        args.hard_stop_runs = std::atoi(hard_cases.data());
      }
      if (ArgValue(argc, argv, "--graceful-runs").empty() &&
          ArgValue(argc, argv, "--graceful-stop-cases").empty()) {
        args.graceful_runs = 3;
      }
      auto grace_cases = ArgValue(argc, argv, "--graceful-stop-cases");
      if (!grace_cases.empty()) {
        args.graceful_runs = std::atoi(grace_cases.data());
      }
      if (ArgValue(argc, argv, "--window-samples-main").empty()) {
        args.window_samples_main = 0;
      }
      if (ArgValue(argc, argv, "--window-samples-extra").empty()) {
        args.window_samples_extra = 0;
      }
      if (args.loss_cases <= 0) {
        args.loss_cases = 2;
      }
    }
    return RunCharacterization(args);
  }

  if (role == "client") {
    ClientArgs args;
    auto side = ArgValue(argc, argv, "--side");
    args.side = (side == "B" || side == "b") ? Side::kB : Side::kA;
    args.run_id = std::string{ArgValue(argc, argv, "--run-id")};
    args.state_dir = std::string{ArgValue(argc, argv, "--state-dir")};
    args.pipe_name = std::string{ArgValue(argc, argv, "--pipe")};
    args.client_name = std::string{ArgValue(argc, argv, "--client-name")};
    args.artifact_dir = std::string{ArgValue(argc, argv, "--artifact-dir")};
    auto parent = ArgValue(argc, argv, "--parent-uid");
    if (!parent.empty()) {
      args.parent_uid = std::string{parent};
    }
    if (args.client_name.empty()) {
      args.client_name =
          args.side == Side::kA ? "uap-1s-alice" : "uap-1s-bob";
    }
    auto ping_ms = ArgValue(argc, argv, "--ping-interval-ms");
    if (!ping_ms.empty()) {
      args.ping_interval_ms = std::strtoll(ping_ms.data(), nullptr, 10);
    } else {
      args.ping_interval_ms = 1000;
    }
    auto rx_ms = ArgValue(argc, argv, "--receive-window-ms");
    if (!rx_ms.empty()) {
      args.receive_window_ms = std::strtoll(rx_ms.data(), nullptr, 10);
    } else {
      args.receive_window_ms = 250;
    }
    return RunClientRole(args);
  }

  std::cerr << "Unknown --role\n";
  return 2;
}
