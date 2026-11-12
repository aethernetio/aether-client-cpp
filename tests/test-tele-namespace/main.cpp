/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>
#include <map>
#include <memory>
#include <string>
#include "aether-tele/raw/decode.h"
#include "aether/aether_tele.h"
#include "app_sink.h"
namespace {
class Backend final : public ae::tele::raw::IBackend {
 public:
  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override {
    return memory.Append(space, bytes);
  }
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<ae::tele::raw::MetricTotal> values,
                      std::size_t& count) override {
    return memory.RestoreMetrics(space, writer, values, count);
  }
  void WriteEnvData(std::string_view space, ae::tele::EnvData const&) override {
    ++environments[std::string{space}];
  }
  ae::tele::raw::MemoryBackend memory;
  std::map<std::string, unsigned> environments;
};
void test_IndependentNamespacesAcrossPublicInlineIncludeOrders() {
  auto backend = std::make_shared<Backend>();
  auto& aether = ae::AetherTeleSink::Instance();
  auto& app = tele_namespace_test::AppSink::Instance();
  TEST_ASSERT_TRUE(aether.SetBackend(backend));
  TEST_ASSERT_TRUE(app.SetBackend(backend));
  aether.SetRuntime({true, false, true, true, ae::tele::raw::kAllFields});
  app.SetRuntime({true, false, true, true, ae::tele::raw::kAllFields});
  tele_namespace_test::RunBefore();
  tele_namespace_test::RunAfter();
  TEST_ASSERT_TRUE(aether.DrainAll());
  TEST_ASSERT_TRUE(app.DrainAll());
  auto app_bytes = backend->memory.Snapshot("app");
  auto app_rows = ae::tele::raw::Decode(app_bytes);
  TEST_ASSERT_TRUE(app_rows.status == ae::tele::raw::DecodeStatus::kOk);
  unsigned app_count{};
  for (auto const& row : app_rows.records) {
    if (row.metric) continue;
    ++app_count;
    std::string text{row.payload.begin(), row.payload.end()};
    TEST_ASSERT_TRUE(text == "app before RunBefore" ||
                     text == "app after RunAfter");
    TEST_ASSERT_TRUE(row.duration_code !=
                     ae::tele::raw::wire::kDurationUnknown);
  }
  TEST_ASSERT_EQUAL_UINT(2, app_count);
  auto aether_bytes = backend->memory.Snapshot("aether");
  auto aether_rows = ae::tele::raw::Decode(aether_bytes);
  TEST_ASSERT_TRUE(aether_rows.status == ae::tele::raw::DecodeStatus::kOk);
  unsigned aether_count{};
  for (auto const& row : aether_rows.records) {
    if (row.metric) continue;
    ++aether_count;
    std::string text{row.payload.begin(), row.payload.end()};
    TEST_ASSERT_TRUE(text.find("app before") == std::string::npos);
    TEST_ASSERT_TRUE(text.find("app after") == std::string::npos);
  }
  constexpr auto error =
      ae::AetherTeleConfig::GetTeleConfig<ae::tele::Level::kError,
                                          ::kLog.module.id>();
  constexpr auto info =
      ae::AetherTeleConfig::GetTeleConfig<ae::tele::Level::kInfo,
                                          ::kLog.module.id>();
  constexpr auto tagged =
      ae::AetherTeleConfig::GetTeleConfig<ae::tele::Level::kInfo,
                                          AetherCreated.module.id>();
  TEST_ASSERT_EQUAL_UINT(
      2 * (unsigned(error.logs_enabled) + unsigned(info.logs_enabled) +
           unsigned(tagged.logs_enabled)),
      aether_count);
  constexpr auto env = ae::AetherTeleConfig::GetEnvConfig();
  TEST_ASSERT_EQUAL_UINT(env.static_info || env.runtime_info ? 2 : 0,
                         backend->environments["aether"]);
  TEST_ASSERT_EQUAL_UINT(0, backend->environments["app"]);
  // Runtime controls remain independent after both include orders.
  app.SetRuntime({false, false, false, false, 0});
  auto app_saved = backend->memory.Snapshot("app");
  tele_namespace_test::RunBefore();
  TEST_ASSERT_TRUE(app.DrainAll());
  TEST_ASSERT_TRUE(aether.DrainAll());
  TEST_ASSERT_TRUE(app_saved == backend->memory.Snapshot("app"));
  TEST_ASSERT_EQUAL_UINT(0, aether.failures());
  TEST_ASSERT_EQUAL_UINT(0, app.failures());
}
}  // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_IndependentNamespacesAcrossPublicInlineIncludeOrders);
  return UNITY_END();
}
