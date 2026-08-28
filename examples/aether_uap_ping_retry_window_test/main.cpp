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
    CoordinatorArgs args;
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
    args.quick = HasFlag(argc, argv, "--quick");
    return RunCoordinator(args);
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
          args.side == Side::kA ? "uap-retry-alice" : "uap-retry-bob";
    }
    auto ping_ms = ArgValue(argc, argv, "--ping-interval-ms");
    if (!ping_ms.empty()) {
      args.ping_interval_ms = std::strtoll(ping_ms.data(), nullptr, 10);
    }
    auto rx_ms = ArgValue(argc, argv, "--receive-window-ms");
    if (!rx_ms.empty()) {
      args.receive_window_ms = std::strtoll(rx_ms.data(), nullptr, 10);
    }
    return RunClientRole(args);
  }

  std::cerr << "Unknown --role\n";
  return 2;
}
