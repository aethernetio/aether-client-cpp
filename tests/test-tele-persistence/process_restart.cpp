/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "aether-objects/domain_storage/file_system_std_storage.h"
#include "aether/aether_app.h"
#include "aether/global_ids.h"
#include "aether/tele.h"
#include "aether/tele_statistics.h"
#include "aether/telemetry_namespaces.h"

namespace ae::test_tele_process_restart {
constexpr unsigned kWorkers = 4, kEvents = 100;
constexpr ObjId kBusinessId{0x051a7e01};
class RestartBusinessCounter final : public Obj {
  AE_OBJECT(RestartBusinessCounter, Obj, 0)
  RestartBusinessCounter() = default;

 public:
  explicit RestartBusinessCounter(ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(counter))
  template <class Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, counter);
  }
  template <class Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_, counter);
  }
  unsigned counter{};
};
void Require(bool value, char const* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
inline constexpr tele::Module kModule{202, 1, 100, "restart-test"};
inline constexpr tele::Tag kTag{7, kModule, "restart-probe"};
void Emit(unsigned boot, unsigned writer, unsigned item) {
  AE_TELE_INFO(kTag, "restart-probe {} {} {}", boot, writer, item);
}
std::unique_ptr<AetherApp> App() {
  auto context =
      AetherAppContext{[] { return std::make_unique<FileSystemStdStorage>(); }};
  auto configured =
      std::move(context)
          .AdaptersFactory([](AetherAppContext const& ctx) {
            auto existing =
                AdapterRegistry::ptr{ctx.aether()->adapter_registry};
            if (existing.is_valid()) {
              return existing;
            }
            return AdapterRegistry::ptr::Create(
                CreateWith{ctx.domain()}.with_id(GlobalId::kAdapterRegistry));
          })
          .CryptoFactory([](auto const&) { return Crypto::ptr{}; })
          .PollerFactory([](auto const&) { return IPoller::ptr{}; });
#if AE_SUPPORT_REGISTRATION
  static_cast<void>(
      std::move(configured).RegistrationCloudFactory([](auto const&) {
        return Cloud::ptr{};
      }));
#endif
#if AE_SUPPORT_CLOUD_DNS
  static_cast<void>(std::move(configured).DnsResolverFactory([](auto const&) {
    return DnsResolver::ptr{};
  }));
#endif
  return AetherApp::Construct(std::move(configured));
}
unsigned Verify(TeleStatisticsBackend& backend, unsigned boots,
                unsigned first_boot = 0) {
  auto bytes = backend.Snapshot("aether");
  auto decoded = tele::raw::Decode(bytes);
  Require(decoded.status == tele::raw::DecodeStatus::kOk,
          "invalid persisted blocks");
  std::optional<std::uint32_t> index;
  unsigned logs = 0;
  std::vector<bool> seen(boots * kWorkers * kEvents);
  for (auto const& row : decoded.records) {
    std::string text(reinterpret_cast<char const*>(row.payload.data()),
                     row.payload.size());
    if (!text.starts_with("restart-probe ")) {
      continue;
    }
    unsigned boot{}, writer{}, item{};
    std::istringstream input{text.substr(14)};
    Require(bool(input >> boot >> writer >> item), "invalid probe payload");
    Require(boot >= first_boot && boot < first_boot + boots &&
                writer < kWorkers && item < kEvents,
            "invalid probe identity");
    auto position = ((boot - first_boot) * kWorkers + writer) * kEvents + item;
    Require(!seen[position], "duplicate persisted log");
    seen[position] = true;
    Require(row.stream == 10 + writer,
            "OS thread ID used instead of logical writer");
    index = row.index;
    ++logs;
  }
  Require(logs == boots * kWorkers * kEvents, "missing persisted logs");
  if (index) {
    auto metric = tele::raw::RestoreMetric(decoded.records, *index);
    Require(metric.count == logs && metric.samples == logs,
            "metric history was lost or replayed");
    for (unsigned writer = 0; writer < kWorkers; ++writer) {
      std::array<tele::raw::MetricTotal, 1024> totals{};
      std::size_t count{};
      Require(backend.RestoreMetrics("aether", 10 + writer, totals, count),
              "writer restore failed");
      auto found = std::find_if(
          totals.begin(), totals.begin() + count,
          [&](auto const& total) { return total.index == *index; });
      Require(found != totals.begin() + count &&
                  found->metric.count == boots * kEvents,
              "incorrect per-writer persisted totals");
    }
  }
  return logs;
}
int Run(unsigned boot, bool abrupt) {
  Require(boot < 4, "invalid restart fixture boot");
  // A compatible source/build change keeps tag semantics; the fourth process
  // changes only the telemetry schema while ordinary business state continues.
  ConfigureProductTelemetry("apptraverse-app", AE_GIT_VERSION,
                            "restart-build-" + std::to_string(boot));
  Require(
      RegisterTelemetryCatalog("restart-test", "fixture", std::string(64, 'd'),
                               std::string(64, boot == 3 ? 'b' : 'a')),
      "catalog registration failed");
  unsigned const first_boot = boot == 3 ? 3 : 0;
  unsigned const previous_boots = boot == 3 ? 0 : boot;
  unsigned const expected_boots = previous_boots + 1;
  {
    auto app = App();
    auto& domain = *app->aether()->domain;
    auto business = RestartBusinessCounter::ptr::Declare(
        CreateWith{domain}.with_id(kBusinessId));
    if (boot == 0) {
      business = RestartBusinessCounter::ptr::Create(
          CreateWith{domain}.with_id(kBusinessId));
    } else {
      Require(bool(business.Load()),
              "independent business object did not load");
    }
    Require(business->counter == boot,
            "business counter was reset with telemetry");
    ++business->counter;
    DomainGraph business_graph{&domain};
    Require(business_graph.SaveRoot(business.Load(), kBusinessId).IsOk(),
            "business counter save failed");
    Require(
        boot == 0 || app->aether()->tele_statistics->telemetry_state_reset() ==
                         (boot == 3),
        "incorrect telemetry compatibility decision");
    auto backend = app->aether()->tele_statistics->backend();
    Verify(*backend, previous_boots, first_boot);
    auto options = AetherTeleSink::Instance().Runtime();
    options.console = false;
    AetherTeleSink::Instance().SetRuntime(options);
    std::atomic<bool> failed{};
    std::array<std::thread, kWorkers> threads;
    std::barrier start{static_cast<std::ptrdiff_t>(kWorkers)};
    // Change physical creation order between processes, keeping writer IDs
    // stable.
    for (unsigned position = 0; position < kWorkers; ++position) {
      auto writer = (position + boot) % kWorkers;
      threads[position] = std::thread([&, writer] {
        start.arrive_and_wait();
        if (!AetherTeleSink::Instance().BindWriter(10 + writer)) {
          failed = true;
          return;
        }
        for (unsigned item = 0; item < kEvents; ++item) {
          Emit(boot, writer, item);
        }
      });
    }
    for (auto& thread : threads) {
      thread.join();
    }
    Require(!failed, "writer binding failed");
    Require(DrainAetherTelemetry(), "checkpoint failed");
    Verify(*backend, expected_boots, first_boot);
    if (abrupt) {
      app->aether().Save();
      std::cout << "CHECKPOINT " << boot + 1 << " BUSINESS "
                << business->counter << " TELEMETRY "
                << expected_boots * kWorkers * kEvents << std::endl;
      std::_Exit(0);
    }
  }  // AetherApp destructor drains telemetry and invokes ordinary ObjPtr::Save.
  FileSystemStdStorage storage;
  Domain domain{storage};
  auto object = TeleStatistics::ptr::Declare(
      CreateWith{domain}.with_id(GlobalId::kTeleStatistics));
  Require(bool(object.Load()), "saved TeleStatistics object did not load");
  Verify(*object->backend(), expected_boots, first_boot);
  auto business = RestartBusinessCounter::ptr::Declare(
      CreateWith{domain}.with_id(kBusinessId));
  Require(bool(business.Load()) && business->counter == boot + 1,
          "saved business counter changed after telemetry reload");
  std::cout << "CHECKPOINT " << boot + 1 << " BUSINESS " << business->counter
            << " TELEMETRY " << expected_boots * kWorkers * kEvents
            << std::endl;
  return 0;
}
}  // namespace ae::test_tele_process_restart
int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      return 2;
    }
    return ae::test_tele_process_restart::Run(
        static_cast<unsigned>(std::stoul(argv[1])), argc > 2);
  } catch (std::exception const& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
