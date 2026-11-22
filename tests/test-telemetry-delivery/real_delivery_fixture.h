// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef TESTS_TELEMETRY_REAL_DELIVERY_FIXTURE_H_
#define TESTS_TELEMETRY_REAL_DELIVERY_FIXTURE_H_
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include "aether-objects/domain_storage/file_system_std_storage.h"
#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "aether/aether_tele.h"
#include "aether/telemetry_delivery.h"
namespace ae::test_telemetry_delivery {
inline Uid RealUid(std::string const& value) {
  if (value.size() != 36) {
    throw std::invalid_argument("UID must have 36 characters");
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    auto c = value[i];
    bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? c != '-'
             : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) {
      throw std::invalid_argument("Invalid UID");
    }
  }
  auto result = Uid::FromString(value);
  if (result.empty()) {
    throw std::invalid_argument("UID must not be empty");
  }
  return result;
}
inline int RealArtifactFixture(int argc, char** argv) {
  std::filesystem::path input_path, profile, sdk_output, map_output;
  std::string product, parent_text, recipient_text;
  unsigned run_seconds = 60;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") {
      std::cout << "test-telemetry-delivery --real --artifact FILE --product "
                   "PRODUCT --profile DIR --parent-uid UID --recipient-uid "
                   "UID [--run-seconds 60] [--sdk-raw-out FILE] "
                   "[--sdk-map-out FILE]\n";
      return 0;
    }
    if (i + 1 == argc) {
      throw std::invalid_argument("Missing real fixture option value");
    }
    std::string value = argv[++i];
    if (arg == "--artifact") {
      input_path = value;
    } else if (arg == "--profile") {
      profile = value;
    } else if (arg == "--sdk-raw-out") {
      sdk_output = value;
    } else if (arg == "--sdk-map-out") {
      map_output = value;
    } else if (arg == "--product") {
      product = value;
    } else if (arg == "--parent-uid") {
      parent_text = value;
    } else if (arg == "--recipient-uid") {
      recipient_text = value;
    } else if (arg == "--run-seconds") {
      run_seconds = static_cast<unsigned>(std::stoul(value));
    } else {
      throw std::invalid_argument("Unknown real fixture option: " + arg);
    }
  }
  if (input_path.empty() || profile.empty() ||
      !ValidTelemetryProduct(product) || !run_seconds || run_seconds > 90) {
    throw std::invalid_argument(
        "Real fixture requires artifact, profile, product and 1..90 second "
        "bound");
  }
#if !AE_FILE_SYSTEM_STD_ENABLED || !AE_FILTRATION || !AE_TELE_ENABLED || \
    !AE_TELE_LOG_TO_STATISTICS
  throw std::runtime_error(
      "Real fixture requires filtration, filesystem storage and telemetry");
#else
  auto parent = RealUid(parent_text);
  auto recipient = RealUid(recipient_text);
  input_path = std::filesystem::absolute(input_path);
  profile = std::filesystem::absolute(profile);
  if (!sdk_output.empty()) {
    sdk_output = std::filesystem::absolute(sdk_output);
  }
  if (!map_output.empty()) {
    map_output = std::filesystem::absolute(map_output);
  }
  auto size = std::filesystem::file_size(input_path);
  if (size > kTelemetryMaxOutboxBytes) {
    throw std::invalid_argument("Real artifact exceeds outbox limit");
  }
  std::ifstream input(input_path, std::ios::binary);
  DataBuffer artifact(std::istreambuf_iterator<char>{input}, {});
  if (input.bad() || artifact.size() != size ||
      !ValidTelemetryArtifact(artifact)) {
    throw std::invalid_argument("Invalid real fixture artifact");
  }
  std::filesystem::create_directories(profile);
  std::filesystem::current_path(profile);
  auto app = AetherApp::Construct(AetherAppContext{});
  auto statistics = app->aether()->tele_statistics;
  std::cout << "REAL_TELEMETRY_STATE_RESET="
            << statistics->telemetry_state_reset() << '\n';
  Client::ptr selected;
  std::unique_ptr<TelemetryDeliverySession> session;
  bool started = false;
  auto on_selection = [&](auto const& result) {
    if (!result) {
      std::cerr << "REAL_SELECTION_FAILED code=" << result.error() << '\n';
      app->Exit(2);
      return;
    }
    selected = result.value();
    auto& outbox = statistics->delivery_outbox();
    if (outbox.pending.empty()) {
      outbox.pending.push_back({NewTelemetryBatchId(), product, artifact});
    } else if (outbox.pending.size() != 1 ||
               outbox.pending.front().product != product ||
               outbox.pending.front().artifact != artifact) {
      std::cerr << "REAL_OUTBOX_MISMATCH\n";
      app->Exit(3);
      return;
    }
    DomainGraph graph{app->aether()->domain};
    if (!graph.SaveRoot(app->aether().Load(), app->aether().id()).IsOk()) {
      std::cerr << "REAL_PROFILE_SAVE_FAILED\n";
      app->Exit(3);
      return;
    }
    std::cout << Format("SENDER_UID={}\n", selected->uid());
    std::cout << "REAL_BATCH_ID="
              << TelemetryBatchIdText(outbox.pending.front().id) << '\n';
    std::cout.flush();
    session = std::make_unique<TelemetryDeliverySession>(
        AeContext{*app}, selected.Load(), recipient, outbox, [statistics] {
          (void)DrainAetherTelemetry();
          DomainGraph graph{statistics->domain};
          return graph.SaveRoot(statistics.Load(), statistics.id()).IsOk();
        });
    started = true;
  };
  // The event map stores a small pointer; its larger callback and all inputs
  // stay in this scope until selection.Reset() below.
  Subscription selection =
      app->aether()
          ->SelectClient(parent, "telemetry-relay-acceptance-sender")
          .result_event()
          .Subscribe([callback = &on_selection](auto const& result) {
            (*callback)(result);
          });
  int exit_code = 4;
  auto end =
      std::chrono::steady_clock::now() + std::chrono::seconds{run_seconds};
  while (!app->IsExited() && std::chrono::steady_clock::now() < end) {
    if (started && statistics->delivery_outbox().pending.empty()) {
      std::cout << "REAL_DURABLE_ACK=1\n";
      exit_code = 0;
      break;
    }
    auto next = app->Update(Now());
    app->WaitUntil(std::min(next, Now() + std::chrono::milliseconds{100}));
  }
  if (app->IsExited()) {
    exit_code = app->ExitCode();
  }
  if (exit_code == 4) {
    std::cerr << (started ? "REAL_ACK_TIMEOUT pending batch retained\n"
                          : "REAL_REGISTRATION_TIMEOUT\n");
  }
  session.reset();
  selection.Reset();
  (void)DrainAetherTelemetry();
  if (!sdk_output.empty()) {
    auto bytes = statistics->backend()->Snapshot("aether");
    std::ofstream output(sdk_output, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      std::cerr << "REAL_SDK_RAW_EXPORT_FAILED\n";
      if (!exit_code) {
        exit_code = 5;
      }
    }
  }
  if (!map_output.empty()) {
    std::ofstream output(map_output, std::ios::binary | std::ios::trunc);
    bool saved =
        AetherTeleSink::Instance().ExportMap(output, "sdk-relay-fixture");
    output.flush();
    if (!saved || !output) {
      std::cerr << "REAL_SDK_MAP_EXPORT_FAILED\n";
      if (!exit_code) {
        exit_code = 5;
      }
    }
  }
  return exit_code;
#endif
}
}  // namespace ae::test_telemetry_delivery
#endif
