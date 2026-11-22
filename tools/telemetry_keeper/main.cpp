// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <algorithm>
#include <cctype>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include "aether-objects/domain_storage/file_system_std_storage.h"
#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "aether/telemetry_keeper.h"
namespace {
volatile std::sig_atomic_t stopping = 0;
void Stop(int) { stopping = 1; }
ae::Uid ParseUid(std::string const& text) {
  if (text.size() != 36) {
    throw std::invalid_argument("UID must have 36 characters");
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    bool dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? text[i] != '-'
             : !std::isxdigit(static_cast<unsigned char>(text[i]))) {
      throw std::invalid_argument("Invalid UID");
    }
  }
  return ae::Uid::FromString(ae::UidString{std::string_view{text}});
}
}  // namespace
int main(int argc, char** argv) {
  try {
    std::filesystem::path profile, storage;
    std::string parent_text, client_id = "telemetry-keeper";
    unsigned run_seconds = 0;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "aether-telemetry-keeper --profile DIR --parent-uid UID "
                     "[--client-id NAME] [--storage DIR] [--run-seconds N]\n"
                     "Uses the real Aether client registration/profile and "
                     "ordinary cloud-relayed client messages.\n";
        return 0;
      }
      if (i + 1 == argc) {
        throw std::invalid_argument("Missing option value");
      }
      std::string value = argv[++i];
      if (arg == "--profile") {
        profile = value;
      } else if (arg == "--storage") {
        storage = value;
      } else if (arg == "--parent-uid") {
        parent_text = value;
      } else if (arg == "--client-id") {
        client_id = value;
      } else if (arg == "--run-seconds") {
        run_seconds = static_cast<unsigned>(std::stoul(value));
      } else {
        throw std::invalid_argument("Unknown option: " + arg);
      }
    }
    if (profile.empty() || parent_text.empty()) {
      throw std::invalid_argument("--profile and --parent-uid are required");
    }
    auto parent = ParseUid(parent_text);
    if (parent.empty()) {
      throw std::invalid_argument("Parent UID must not be empty");
    }
#if AE_DISTILLATION && !AE_FILTRATION
    throw std::runtime_error(
        "Build the keeper with AE_FILTRATION=ON to preserve its registered "
        "identity");
#endif
#if !AE_FILE_SYSTEM_STD_ENABLED
    throw std::runtime_error(
        "Build the keeper with filesystem profile storage enabled");
#endif
    profile = std::filesystem::absolute(profile);
    storage = storage.empty() ? profile / "received"
                              : std::filesystem::absolute(storage);
    std::filesystem::create_directories(profile);
    std::filesystem::current_path(profile);
    auto app = ae::AetherApp::Construct(ae::AetherAppContext{});
    ae::TelemetryKeeperStore store{storage};
    std::unique_ptr<ae::TelemetryKeeperSession> keeper;
    ae::Client::ptr selected;
    auto& selection = app->aether()->SelectClient(parent, client_id);
    ae::Subscription subscription =
        selection.result_event().Subscribe([&](auto const& result) {
          if (!result) {
            std::cerr << "telemetry keeper: client selection failed code="
                      << result.error() << '\n';
            app->Exit(2);
            return;
          }
          selected = result.value();
          ae::DomainGraph graph{app->aether()->domain};
          if (!graph.SaveRoot(app->aether().Load(), app->aether().id())
                   .IsOk()) {
            std::cerr << "telemetry keeper: identity profile save failed\n";
            app->Exit(3);
            return;
          }
          keeper = std::make_unique<ae::TelemetryKeeperSession>(
              ae::AeContext{*app}, selected.Load(), store);
          ae::Format(std::cout, "KEEPER_UID={}\n", selected->uid());
          std::cout.flush();
        });
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    auto start = std::chrono::steady_clock::now();
    while (!app->IsExited() && !stopping) {
      if (run_seconds && std::chrono::steady_clock::now() - start >=
                             std::chrono::seconds{run_seconds}) {
        break;
      }
      auto next = app->Update(ae::Now());
      app->WaitUntil(
          std::min(next, ae::Now() + std::chrono::milliseconds{500}));
    }
    if (run_seconds && !app->IsExited() && !keeper) {
      std::cerr << "telemetry keeper: registration did not complete within "
                   "--run-seconds\n";
      return 2;
    }
    keeper.reset();
    return app->IsExited() ? app->ExitCode() : 0;
  } catch (std::exception const& error) {
    std::cerr << "telemetry keeper: " << error.what() << '\n';
    return 1;
  }
}
