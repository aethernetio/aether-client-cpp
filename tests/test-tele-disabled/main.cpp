/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <latch>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/telemetry.h"
#include "aether/aether_app.h"
#include "aether/aether_tele.h"
#include "aether/global_ids.h"

#if AE_TELE_ENABLED || AE_TELE_LOG_TO_STATISTICS || AE_TELE_LOG_CONSOLE
#  error "Configure production Aether with tests/test-tele-disabled/config.h"
#endif
#if !AE_OBJECTS_RAW_TELE
#  error \
      "The disabled-Aether regression also verifies independent Objects telemetry"
#endif

namespace ae::test_tele_disabled {
class Backend final : public tele::raw::IBackend {
 public:
  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override {
    ++appends;
    return memory.Append(space, bytes);
  }
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<tele::raw::MetricTotal> values,
                      std::size_t& count) override {
    ++restores;
    return memory.RestoreMetrics(space, writer, values, count);
  }
  void Console(std::string_view, std::span<std::uint8_t const>) override {
    ++consoles;
  }
  void WriteEnvData(std::string_view, tele::EnvData const&) override {
    ++environments;
  }
  tele::raw::MemoryBackend memory;
  std::atomic<unsigned> appends{}, restores{}, consoles{}, environments{};
};

std::unique_ptr<AetherApp> MakeApp() {
  auto context =
      AetherAppContext{[] { return std::make_unique<RamDomainStorage>(); }};
  auto configured =
      std::move(context)
          .AdaptersFactory([](AetherAppContext const& context) {
            return AdapterRegistry::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    GlobalId::kAdapterRegistry));
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

void test_TwoLiveApplicationsDoNotOwnDisabledTelemetryOrOtherNamespaces() {
  auto aether_backend = std::make_shared<Backend>();
  auto objects_backend = std::make_shared<Backend>();
  auto& aether = AetherTeleSink::Instance();
  auto& objects = objects_tele::ObjectTeleSink::Instance();
  TEST_ASSERT_TRUE(aether.SetBackend(aether_backend));
  TEST_ASSERT_TRUE(objects.SetBackend(objects_backend));
  // Sentinels detect shutdown disabling a namespace that the app does not own.
  aether.SetRuntime({true, false, true, true, tele::raw::kAllFields});
  objects.SetRuntime({true, false, true, true, tele::raw::kAllFields});
  std::latch first_ready{1};
  std::latch second_ready{1};
  std::barrier destroy_together{2};
  std::atomic<bool> valid{true};
  std::thread first{[&] {
    auto app = MakeApp();
    if (!app) valid = false;
    first_ready.count_down();
    second_ready.wait();
    destroy_together.arrive_and_wait();
    app.reset();
  }};
  std::thread second{[&] {
    first_ready.wait();
    // The first app is still alive on its owner thread. The old code attempts
    // another BindWriter(0) here and aborts/throws even with every output off.
    auto app = MakeApp();
    if (!app) valid = false;
    second_ready.count_down();
    destroy_together.arrive_and_wait();
    app.reset();
  }};
  first.join();
  second.join();
  TEST_ASSERT_TRUE(valid.load());
  TEST_ASSERT_TRUE(aether.GetBackend() == aether_backend);
  TEST_ASSERT_TRUE(objects.GetBackend() == objects_backend);
  TEST_ASSERT_TRUE(aether.Runtime().logs);
  TEST_ASSERT_TRUE(objects.Runtime().logs);
  for (auto const& backend : {aether_backend, objects_backend}) {
    TEST_ASSERT_EQUAL_UINT(0, backend->appends.load());
    TEST_ASSERT_EQUAL_UINT(0, backend->restores.load());
    TEST_ASSERT_EQUAL_UINT(0, backend->consoles.load());
    TEST_ASSERT_EQUAL_UINT(0, backend->environments.load());
  }
  // The public drain remains an explicit cross-namespace operation even when
  // automatic Aether telemetry is compiled out.
  {
    AE_TELE_DEBUG_TO(objects_tele::ObjectTeleSink, objects_tele::kRamSaved,
                     "independent Objects data");
  }
  TEST_ASSERT_TRUE(DrainAetherTelemetry());
  TEST_ASSERT_TRUE(objects_backend->appends.load() != 0);
  TEST_ASSERT_FALSE(objects_backend->memory.Snapshot("objects").empty());
  TEST_ASSERT_EQUAL_UINT(0, aether_backend->appends.load());
}
}  // namespace ae::test_tele_disabled

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_tele_disabled::
          test_TwoLiveApplicationsDoNotOwnDisabledTelemetryOrOtherNamespaces);
  return UNITY_END();
}
