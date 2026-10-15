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

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#define AE_EXAMPLE_ETHERNET 1

#include "../../common/aether_construct_ethernet.h"

#include "stream_matrix/bench_persistence.h"
#include "stream_matrix/bench_shared.h"

namespace {

using ae::stream_matrix_bench::BootstrapMarker;
using ae::stream_matrix_bench::DomainStateHasPersistedObjects;
using ae::stream_matrix_bench::kBootstrapMarkerFile;
using ae::stream_matrix_bench::kDefaultDomainStateDir;
using ae::stream_matrix_bench::kStreamMatrixReceiverId;
using ae::stream_matrix_bench::kStreamMatrixSenderId;
using ae::stream_matrix_bench::ReadBootstrapMarker;
using ae::stream_matrix_bench::StreamMatrixParentUid;
using ae::stream_matrix_bench::WriteBootstrapMarker;

struct Options {
  bool verify_only{false};
  bool force_register{false};
};

Options ParseArgs(int argc, char* argv[]) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--verify") {
      opt.verify_only = true;
    } else if (arg == "--force-register") {
      opt.force_register = true;
    }
  }
  return opt;
}

bool PumpUntil(ae::AetherApp& app, auto predicate, ae::Duration timeout) {
  auto deadline = ae::Now() + timeout;
  while (!app.IsExited()) {
    if (predicate()) {
      return true;
    }
    if (ae::Now() >= deadline) {
      return false;
    }
    app.Update(ae::Now());
    app.WaitUntil(ae::Now() + std::chrono::milliseconds{5});
  }
  return false;
}

bool AcquireBothClients(ae::AetherApp& app, ae::Client::ptr& a,
                        ae::Client::ptr& b) {
  app.aether()->SelectClient(StreamMatrixParentUid(), kStreamMatrixSenderId)
      .result_event()
      .Subscribe([&](auto const& res) {
        if (res) {
          a = res.value();
        }
      });
  app.aether()->SelectClient(StreamMatrixParentUid(), kStreamMatrixReceiverId)
      .result_event()
      .Subscribe([&](auto const& res) {
        if (res) {
          b = res.value();
        }
      });
  return PumpUntil(app, [&] { return a && b; }, std::chrono::seconds{180});
}

}  // namespace

int main(int argc, char* argv[]) {
  auto const opt = ParseArgs(argc, argv);
  auto marker_path = std::filesystem::path{kBootstrapMarkerFile};

  if (opt.verify_only) {
    if (!DomainStateHasPersistedObjects()) {
      std::cerr << "bootstrap verify: no domain state in " << kDefaultDomainStateDir
                << '\n';
      return 1;
    }
    auto app = ae::examples::construct_aether_app();
    ae::Client::ptr sender;
    ae::Client::ptr receiver;
    if (!AcquireBothClients(*app, sender, receiver)) {
      std::cerr << "bootstrap verify: failed to load clients from state\n";
      return 1;
    }
    std::cout << "bootstrap verify: ok\n";
    app->Exit(0);
    return 0;
  }

  if (!opt.force_register && DomainStateHasPersistedObjects()) {
    if (auto marker = ReadBootstrapMarker(marker_path)) {
      std::cout << "bootstrap: reuse existing state (marker present)\n";
      return 0;
    }
  }

  auto app = ae::examples::construct_aether_app();
  ae::Client::ptr sender;
  ae::Client::ptr receiver;
  if (!AcquireBothClients(*app, sender, receiver)) {
    std::cerr << "bootstrap: registration failed\n";
    app->Exit(1);
    return 1;
  }
  BootstrapMarker marker{
      .parent_uid = "3ac93165-3d37-4970-87a6-fa4ee27744e4",
      .sender_client_id = kStreamMatrixSenderId,
      .receiver_client_id = kStreamMatrixReceiverId,
      .sender_uid = "ok",
      .receiver_uid = "ok",
  };
  WriteBootstrapMarker(marker_path, marker);
  std::cout << "bootstrap: registered and saved state\n";
  app->Exit(0);
  return 0;
}
