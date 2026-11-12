/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-tele/configs/all_enabled.h"
#include "aether-tele/defines.h"
#include "aether-tele/raw/decode.h"
#include "aether-tele/raw/sink.h"
#include "aether/aether_app.h"
#include "aether/aether_tele.h"
#include "aether/tele_statistics_backend.h"

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  error "This consumer must compile without C++ exceptions"
#endif
#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
#  error "This consumer must compile without C++ RTTI"
#endif

namespace ae::test_tele_no_runtime {
struct Identity {
  static constexpr std::string_view kName = "no-runtime";
};
using Sink = tele::RawTeleSink<Identity, tele::AllEnabledConfig>;

class RetryBackend final : public tele::raw::IBackend {
 public:
  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override {
    if (reject) {
      pending.assign(bytes.begin(), bytes.end());
      return false;
    }
    if (!pending.empty()) {
      exact = pending == std::vector<std::uint8_t>(bytes.begin(), bytes.end());
      pending.clear();
    }
    return memory.Append(space, bytes);
  }
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<tele::raw::MetricTotal> values,
                      std::size_t& count) override {
    return memory.RestoreMetrics(space, writer, values, count);
  }
  tele::raw::MemoryBackend memory;
  std::vector<std::uint8_t> pending;
  bool reject{true};
  bool exact{true};
};

void test_FailedDrainRetriesIdenticalBytesWithoutExceptions() {
  auto backend = std::make_shared<RetryBackend>();
  auto& sink = Sink::Instance();
  TEST_ASSERT_TRUE(sink.SetBackend(backend));
  TEST_ASSERT_TRUE(sink.BindWriter(3));
  sink.SetRuntime({true, false, true, true, tele::raw::kAllFields});
  {
    AE_TELE_INFO_TO(Sink, kLog, "no runtime {}", 7);
  }
  TEST_ASSERT_FALSE(sink.DrainAll());
  TEST_ASSERT_FALSE(backend->pending.empty());
  TEST_ASSERT_TRUE(backend->memory.Snapshot(Identity::kName).empty());
  backend->reject = false;
  TEST_ASSERT_TRUE(sink.DrainAll());
  TEST_ASSERT_TRUE(backend->exact);
  auto const bytes = backend->memory.Snapshot(Identity::kName);
  auto const decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  unsigned logs{};
  for (auto const& record : decoded.records) {
    if (!record.metric.has_value()) {
      ++logs;
      TEST_ASSERT_TRUE(std::string(record.payload.begin(),
                                   record.payload.end()) == "no runtime 7");
      auto const total =
          tele::raw::RestoreMetric(decoded.records, record.index);
      TEST_ASSERT_TRUE(total.count == 1);
    }
  }
  TEST_ASSERT_EQUAL_UINT(1, logs);
}

void test_BackendTypeTokenWorksAcrossLibraryBoundaryWithoutRtti() {
  // The factory is compiled in Aether; the expected token is accessed in this
  // consumer. Both must refer to the same type despite different runtime flags.
  auto backend = MakeAetherTelemetryBackend();
  TEST_ASSERT_TRUE(backend->TypeToken() ==
                   TeleStatisticsBackend::StaticTypeToken());
  auto typed = std::static_pointer_cast<TeleStatisticsBackend>(backend);
  TEST_ASSERT_TRUE(typed->Snapshot().spaces.empty());
  tele::raw::MemoryBackend other;
  TEST_ASSERT_NULL(other.TypeToken());
  TEST_ASSERT_TRUE(other.TypeToken() !=
                   TeleStatisticsBackend::StaticTypeToken());
}
void test_StartupReplacesUnknownBackendWithCheckedPersistentType() {
  auto previous = std::make_shared<tele::raw::MemoryBackend>();
  auto& sink = AetherTeleSink::Instance();
  TEST_ASSERT_TRUE(sink.SetBackend(previous));
  AetherAppContext context{[] { return std::make_unique<RamDomainStorage>(); }};
  auto const backend = sink.GetBackend();
  TEST_ASSERT_TRUE(backend != previous);
  TEST_ASSERT_TRUE(backend->TypeToken() ==
                   TeleStatisticsBackend::StaticTypeToken());
  TEST_ASSERT_TRUE(sink.DrainAll());
}
}  // namespace ae::test_tele_no_runtime

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_tele_no_runtime::
               test_FailedDrainRetriesIdenticalBytesWithoutExceptions);
  RUN_TEST(ae::test_tele_no_runtime::
               test_BackendTypeTokenWorksAcrossLibraryBoundaryWithoutRtti);
  RUN_TEST(ae::test_tele_no_runtime::
               test_StartupReplacesUnknownBackendWithCheckedPersistentType);
  return UNITY_END();
}
