/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-tele/raw.h"
#include "aether-tele/raw/decode.h"
#include "aether/ae_actions/ping.h"
#include "aether/ae_actions/telemetry_payload.h"
#include "aether/aether_app.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/global_ids.h"
#include "aether/tele.h"
#if AE_OBJECTS_RAW_TELE
#  include "aether-objects/telemetry.h"
#endif
#include "aether/tele_statistics.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#if AE_ENABLE_PING
namespace ae {
struct PingTestAccess {
  static void Arm(Ping& ping, ClientApiSafe& api, TimePoint time) {
    ping.ArmResponse(api, time);
  }
  static bool Waiting(Ping const& ping) { return bool(ping.wait_result_sub_); }
};
}  // namespace ae
#endif

namespace ae::test_tele_persistence {
struct Identity {};
using Space =
    tele::raw::Space<Identity, tele::raw::Options{false, false, true, true, 0}>;
struct Destination {
  std::shared_ptr<TeleStatisticsBackend> backend;
  std::string space{"aether"};
  bool fail{};
  std::vector<std::uint8_t> rejected;
  bool exact{true};
  static bool Save(void* context, std::span<std::uint8_t const> bytes) {
    auto& self = *static_cast<Destination*>(context);
    if (self.fail) {
      self.fail = false;
      self.rejected.assign(bytes.begin(), bytes.end());
      return false;
    }
    if (!self.rejected.empty()) {
      self.exact =
          self.exact && std::vector<std::uint8_t>(bytes.begin(), bytes.end()) ==
                            self.rejected;
      self.rejected.clear();
    }
    return self.backend->Append(self.space, bytes);
  }
};
void AssertMetric(tele::raw::Metric const& expected,
                  tele::raw::Metric const& actual) {
  TEST_ASSERT_TRUE(expected.count == actual.count);
  TEST_ASSERT_TRUE(expected.samples == actual.samples);
  TEST_ASSERT_TRUE(expected.sum == actual.sum);
  TEST_ASSERT_TRUE(expected.min == actual.min);
  TEST_ASSERT_TRUE(expected.max == actual.max);
}
std::vector<tele::raw::MetricTotal> Restore(TeleStatisticsBackend& backend,
                                            std::string const& name,
                                            std::uint32_t writer) {
  std::array<tele::raw::MetricTotal, 128> output{};
  std::size_t count{};
  auto const restored = backend.RestoreMetrics(name, writer, output, count);
  TEST_ASSERT_TRUE(restored);
  return {output.begin(), output.begin() + count};
}
tele::raw::Metric Total(TeleStatisticsBackend& backend,
                        std::string const& name) {
  auto bytes = backend.Snapshot(name);
  auto decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  return tele::raw::RestoreMetric(decoded.records, 7);
}
TeleStatisticsArchive RoundTrip(TeleStatisticsArchive const& source) {
  std::vector<std::uint8_t> bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save.Save(source).IsOk());
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TeleStatisticsArchive result;
  TEST_ASSERT_TRUE(load.Load(result).IsOk());
  return result;
}

void test_SeveralRestartsContinueOnlyAcknowledgedDeltas() {
  TeleStatisticsArchive persisted;
  for (std::uint32_t boot = 0; boot < 5; ++boot) {
    auto backend = std::make_shared<TeleStatisticsBackend>(false);
    TEST_ASSERT_TRUE(backend->RestoreArchive(persisted));
    Space space;
    Destination destination{backend};
    tele::raw::Stream<Space, 128> stream(space, 3, Destination::Save,
                                         &destination);
    auto totals = Restore(*backend, "aether", 3);
    TEST_ASSERT_TRUE(stream.RestoreMetrics(totals));
    AssertMetric(boot == 0
                     ? tele::raw::Metric{}
                     : tele::raw::Metric{2 * boot, 2 * boot, 12 * boot, 5, 7},
                 stream.MetricFor(7));
    auto before = backend->Snapshot("aether");
    TEST_ASSERT_TRUE(stream.DrainMetrics());
    TEST_ASSERT_TRUE(before == backend->Snapshot("aether"));
    stream.Count(7);
    stream.AddDuration(7, 5);
    destination.fail = true;
    TEST_ASSERT_FALSE(stream.DrainMetrics());
    stream.Count(7);
    stream.AddDuration(7, 7);
    TEST_ASSERT_TRUE(stream.DrainMetrics());
    TEST_ASSERT_TRUE(destination.exact);
    persisted = RoundTrip(backend->Snapshot());
    AssertMetric({2 * (boot + 1), 2 * (boot + 1), 12 * (boot + 1), 5, 7},
                 Total(*backend, "aether"));
  }
}

void test_EarlyMergeIsIdempotentAndForwardsLateScopeDeltas() {
  auto saved = std::make_shared<TeleStatisticsBackend>(false);
  auto early = std::make_shared<TeleStatisticsBackend>(false);
  Space space;
  Destination a{saved}, b{early};
  tele::raw::Stream<Space, 128> history(space, 3, Destination::Save, &a);
  history.Count(7);
  history.AddDuration(7, 10);
  TEST_ASSERT_TRUE(history.DrainMetrics());
  tele::raw::Stream<Space, 128> live(space, 3, Destination::Save, &b);
  live.Count(7);
  TEST_ASSERT_TRUE(live.DrainMetrics());
  TEST_ASSERT_TRUE(saved->MergeEarly(early));
  TEST_ASSERT_TRUE(saved->MergeEarly(early));
  live.AddDuration(7, 5);
  TEST_ASSERT_TRUE(live.DrainMetrics());
  AssertMetric({2, 2, 15, 5, 10}, Total(*saved, "aether"));
  TEST_ASSERT_TRUE(early->Snapshot("aether") == saved->Snapshot("aether"));
  TEST_ASSERT_FALSE(early->MergeEarly(saved));
}

void test_ConcurrentAppendSnapshotAndEarlyMergePreserveEveryWriterDelta() {
  auto persisted = std::make_shared<TeleStatisticsBackend>(false);
  auto early = std::make_shared<TeleStatisticsBackend>(false);
  std::atomic<bool> start{false}, valid{true};
  std::atomic<unsigned> complete{};
  std::array<std::thread, 4> workers;
  for (unsigned writer = 0; writer < workers.size(); ++writer) {
    workers[writer] = std::thread{[&, writer]() {
      Space space;
      Destination destination{early};
      tele::raw::Stream<Space, 128> stream(space, writer, Destination::Save,
                                           &destination);
      while (!start.load()) {
        std::this_thread::yield();
      }
      for (unsigned event = 0; event < 200; ++event) {
        stream.Count(7);
        stream.AddDuration(7, writer + 1);
        if (!stream.DrainMetrics()) {
          valid.store(false);
        }
      }
      ++complete;
    }};
  }
  start.store(true);
  auto const merged = persisted->MergeEarly(early);
  while (complete.load() != workers.size()) {
    auto snapshot = persisted->Snapshot();
    if (!ValidTeleStatisticsArchive(snapshot)) {
      valid.store(false);
    }
    std::this_thread::yield();
  }
  for (auto& worker : workers) {
    worker.join();
  }
  TEST_ASSERT_TRUE(merged);
  TEST_ASSERT_TRUE(valid.load());
  auto restored = std::make_shared<TeleStatisticsBackend>(false);
  TEST_ASSERT_TRUE(restored->RestoreArchive(RoundTrip(persisted->Snapshot())));
  for (unsigned writer = 0; writer < workers.size(); ++writer) {
    auto totals = Restore(*restored, "aether", writer);
    TEST_ASSERT_EQUAL_UINT(1, totals.size());
    AssertMetric({200, 200, 200 * (writer + 1), writer + 1, writer + 1},
                 totals.front().metric);
  }
  AssertMetric({800, 800, 2000, 1, 4}, Total(*restored, "aether"));
}

void test_InvalidArchivesAndFailedAppendsDoNotReplaceHistory() {
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  Space space;
  Destination destination{backend};
  tele::raw::Stream<Space, 128> stream(space, 3, Destination::Save,
                                       &destination);
  stream.Count(7);
  stream.AddDuration(7, 17);
  TEST_ASSERT_TRUE(stream.DrainMetrics());
  auto original = backend->Snapshot("aether");
  std::array<std::uint8_t, 1> malformed{0};
  TEST_ASSERT_FALSE(backend->Append("aether", malformed));
  auto invalid = backend->Snapshot();
  invalid.spaces["aether"].blocks.pop_back();
  TEST_ASSERT_FALSE(backend->RestoreArchive(std::move(invalid)));
  TEST_ASSERT_TRUE(original == backend->Snapshot("aether"));
  auto limited =
      std::make_shared<TeleStatisticsBackend>(false, original.size() - 1);
  TEST_ASSERT_FALSE(limited->Append("aether", original));
  TEST_ASSERT_TRUE(limited->Snapshot().spaces.empty());
  TEST_ASSERT_FALSE(limited->MergeEarly(backend));
  TEST_ASSERT_TRUE(original == backend->Snapshot("aether"));
  std::array<tele::raw::MetricTotal, 0> no_output;
  std::size_t count = 11;
  TEST_ASSERT_FALSE(backend->RestoreMetrics("aether", 3, no_output, count));
  TEST_ASSERT_EQUAL_UINT(0, count);
}

void test_ObjDomainSaveLoadContinuesWithoutStop() {
  RamDomainStorage storage;
  for (std::uint32_t boot = 0; boot < 4; ++boot) {
    Domain domain{storage};
    auto object =
        boot == 0
            ? TeleStatistics::ptr::Create(CreateWith{domain}.with_id(123))
            : TeleStatistics::ptr::Declare(CreateWith{domain}.with_id(123));
    object.Load();
    auto backend = object->backend();
    Space space;
    Destination destination{backend};
    tele::raw::Stream<Space, 128> stream(space, 3, Destination::Save,
                                         &destination);
    auto totals = Restore(*backend, "aether", 3);
    TEST_ASSERT_TRUE(stream.RestoreMetrics(totals));
    AssertMetric(boot == 0 ? tele::raw::Metric{}
                           : tele::raw::Metric{boot, boot, 5 * boot, 5, 5},
                 stream.MetricFor(7));
    stream.Count(7);
    stream.AddDuration(7, 5);
    TEST_ASSERT_TRUE(stream.DrainMetrics());
    object.Save();
  }
}

void test_ObjectRestartAndDrainStartIndependentTimeAnchors() {
  RamDomainStorage storage;
  constexpr std::array<std::uint64_t, 4> bases{
      1000000, 2000000, 100, 999999999999};
  using LogSpace = tele::raw::Space<Identity>;
  for (std::size_t boot = 0; boot < bases.size(); ++boot) {
    Domain domain{storage};
    auto object = boot == 0
        ? TeleStatistics::ptr::Create(CreateWith{domain}.with_id(124))
        : TeleStatistics::ptr::Declare(CreateWith{domain}.with_id(124));
    TEST_ASSERT_TRUE(bool(object.Load()));
    auto backend = object->backend();
    LogSpace space;
    space.SetRuntime({true, false, true, true, tele::raw::kTime});
    Destination destination{backend};
    tele::raw::Stream<LogSpace, 128> stream(space, 3, Destination::Save,
                                          &destination);
    auto totals = Restore(*backend, "aether", 3);
    TEST_ASSERT_TRUE(stream.RestoreMetrics(totals));
    TEST_ASSERT_TRUE(stream.MetricFor(7).count == 3 * boot);
    TEST_ASSERT_TRUE(stream.MetricFor(7).sum == 15 * boot);
    tele::raw::Tag<LogSpace> const tag{
        7, 1, 1, "restart-time", "restart-time", __FILE__, __LINE__};
    for (std::uint64_t event = 0; event < 3; ++event) {
      TEST_ASSERT_TRUE(stream.Emit(tag, tele::raw::Kind::kLog,
          space.Runtime(), {}, 0, bases[boot] + event));
      stream.Count(7);
      stream.AddDuration(7, 5);
      if (event == 1) {
        destination.fail = true;
        TEST_ASSERT_FALSE(stream.Drain());
        TEST_ASSERT_TRUE(stream.Drain());
        TEST_ASSERT_TRUE(destination.exact);
      }
    }
    TEST_ASSERT_TRUE(stream.DrainMetrics());
    auto bytes = backend->Snapshot("aether");
    auto decoded = tele::raw::Decode(bytes);
    TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
    std::size_t logs = 0;
    for (auto const& record : decoded.records) {
      if (record.kind != tele::raw::Kind::kLog) continue;
      TEST_ASSERT_TRUE(record.time.has_value());
      TEST_ASSERT_TRUE(*record.time == bases[logs / 3] + logs % 3);
      ++logs;
    }
    TEST_ASSERT_TRUE(logs == 3 * (boot + 1));
    auto restored = Restore(*backend, "aether", 3);
    TEST_ASSERT_TRUE(restored.size() == 1);
    AssertMetric({3 * (boot + 1), 3 * (boot + 1), 15 * (boot + 1), 5, 5},
                 restored.front().metric);
    object.Save();  // No stop event or live-stream state is persisted.
  }
}

void test_NamespacesAndWritersRemainIndependent() {
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  Space space;
  Destination a{backend, "aether"}, b{backend, "objects"};
  tele::raw::Stream<Space, 128> first(space, 3, Destination::Save, &a);
  tele::raw::Stream<Space, 128> second(space, 3, Destination::Save, &b);
  first.Count(7);
  first.AddDuration(7, 5);
  second.Count(7);
  second.AddDuration(7, 9);
  TEST_ASSERT_TRUE(first.DrainMetrics());
  TEST_ASSERT_TRUE(second.DrainMetrics());
  AssertMetric({1, 1, 5, 5, 5}, Total(*backend, "aether"));
  AssertMetric({1, 1, 9, 9, 9}, Total(*backend, "objects"));
  TEST_ASSERT_TRUE(Restore(*backend, "aether", 99).empty());
}

void test_ArchiveLargerThanDefaultContainerGuardLoadsInChunks() {
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  Space space;
  Destination destination{backend};
  tele::raw::Stream<Space, 128> stream(space, 3, Destination::Save,
                                       &destination);
  stream.Count(7);
  TEST_ASSERT_TRUE(stream.DrainMetrics());
  auto archive = backend->Snapshot();
  auto block = archive.spaces["aether"].blocks;
  auto& bytes = archive.spaces["aether"].blocks;
  while (bytes.size() <= seri::kDefaultBinaryArchiveMaxContainerLoadSize) {
    bytes.insert(bytes.end(), block.begin(), block.end());
  }
  auto restored = RoundTrip(archive);
  TEST_ASSERT_TRUE(restored.spaces["aether"].blocks == bytes);
}

void test_CatalogChangesArchiveHistoryWithoutMisattributingNewIndices() {
  auto a = std::make_shared<TeleStatisticsBackend>(
      false, std::numeric_limits<std::size_t>::max(), "catalog-A");
  Space space;
  Destination destination{a};
  tele::raw::Stream<Space, 128> first(space, 3, Destination::Save,
                                      &destination);
  first.Count(7);
  first.AddDuration(7, 5);
  TEST_ASSERT_TRUE(first.DrainMetrics());
  auto archived = RoundTrip(a->Snapshot());
  auto b = std::make_shared<TeleStatisticsBackend>(
      false, std::numeric_limits<std::size_t>::max(), "catalog-B");
  TEST_ASSERT_TRUE(b->RestoreArchive(archived));
  TEST_ASSERT_TRUE(Restore(*b, "aether", 3).empty());
  AssertMetric({1, 1, 5, 5, 5}, Total(*b, "aether@catalog-A"));
  TEST_ASSERT_FALSE(b->MergeEarly(a));
  Destination next{b};
  tele::raw::Stream<Space, 128> second(space, 3, Destination::Save, &next);
  TEST_ASSERT_TRUE(second.RestoreMetrics(Restore(*b, "aether", 3)));
  second.Count(7);
  second.AddDuration(7, 9);
  TEST_ASSERT_TRUE(second.DrainMetrics());
  AssertMetric({1, 1, 9, 9, 9}, Total(*b, "aether"));
  auto reopened = std::make_shared<TeleStatisticsBackend>(
      false, std::numeric_limits<std::size_t>::max(), "catalog-B");
  TEST_ASSERT_TRUE(reopened->RestoreArchive(RoundTrip(b->Snapshot())));
  AssertMetric({1, 1, 9, 9, 9}, Total(*reopened, "aether"));
  AssertMetric({1, 1, 5, 5, 5}, Total(*reopened, "aether@catalog-A"));
}

void test_TruncatedOrMalformedArchiveLoadLeavesDestinationUnchanged() {
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  Space space;
  Destination destination{backend};
  tele::raw::Stream<Space, 128> stream(space, 3, Destination::Save,
                                       &destination);
  stream.Count(7);
  TEST_ASSERT_TRUE(stream.DrainMetrics());
  auto baseline = backend->Snapshot();
  std::vector<std::uint8_t> saved;
  auto output = seri::BinaryArchive{seri::BinaryVectorBuffer<>{saved}};
  TEST_ASSERT_TRUE(output.Save(baseline).IsOk());
  for (std::size_t cut = 0; cut < saved.size(); ++cut) {
    std::vector<std::uint8_t> torn(saved.begin(), saved.begin() + cut);
    auto input = seri::BinaryArchive{seri::BinaryVectorBuffer<>{torn}};
    auto loaded = baseline;
    TEST_ASSERT_TRUE(input.Load(loaded).IsErr());
    TEST_ASSERT_TRUE(loaded.spaces["aether"].blocks ==
                     baseline.spaces["aether"].blocks);
    TEST_ASSERT_TRUE(loaded.spaces["aether"].catalog ==
                     baseline.spaces["aether"].catalog);
  }
  auto malformed = baseline;
  malformed.spaces["aether"].blocks[0] = 0;
  std::vector<std::uint8_t> bad;
  auto bad_output = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bad}};
  TEST_ASSERT_TRUE(bad_output.Save(malformed).IsOk());
  auto bad_input = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bad}};
  auto loaded = baseline;
  TEST_ASSERT_TRUE(bad_input.Load(loaded).IsErr());
  TEST_ASSERT_TRUE(loaded.spaces["aether"].blocks ==
                   baseline.spaces["aether"].blocks);
}

std::size_t PacketSize(Telemetric const& payload) {
  AuthorizedApi api;
  ApiContext context{api};
  context->SendTelemetry(payload);
  return std::move(context).Pack().size();
}
Telemetric EncodeFragment(TelemetryFragment const& fragment) {
  Telemetric payload{};
  auto writer =
      seri::BinaryArchive{seri::BinaryVectorBuffer<>{payload.cpp.blob}};
  TEST_ASSERT_TRUE(writer.Save(fragment).IsOk());
  return payload;
}
TeleStatisticsArchive LargePayloadArchive() {
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  using LogSpace = tele::raw::Space<Identity>;
  LogSpace space;
  Destination destination{backend};
  tele::raw::Stream<LogSpace, 8192> stream(space, 3, Destination::Save,
                                           &destination);
  tele::raw::Tag<LogSpace> const tag{7,       1,        1,       "large",
                                     "large", __FILE__, __LINE__};
  std::vector<std::uint8_t> blob(6000, 42);
  TEST_ASSERT_TRUE(
      stream.Emit(tag, tele::raw::Kind::kLog, space.Runtime(), {blob}, 5, 100));
  TEST_ASSERT_TRUE(stream.Drain());
  auto archive = backend->Snapshot();
  archive.spaces["aether"].environment.compiler = std::string(3000, 'x');
  archive.spaces["objects"] = archive.spaces["aether"];
  archive.spaces["objects"].catalog = "objects-v1";
  return archive;
}
void AssertArchiveEqual(TeleStatisticsArchive const& expected,
                        TeleStatisticsArchive const& actual) {
  TEST_ASSERT_EQUAL_UINT(expected.spaces.size(), actual.spaces.size());
  for (auto const& [name, value] : expected.spaces) {
    auto found = actual.spaces.find(name);
    TEST_ASSERT_TRUE(found != actual.spaces.end());
    TEST_ASSERT_TRUE(found->second.blocks == value.blocks);
    TEST_ASSERT_TRUE(found->second.catalog == value.catalog);
    TEST_ASSERT_TRUE(found->second.environment.compiler ==
                     value.environment.compiler);
  }
}
void test_TransportFragmentsHandleLargeRowsAndEnvironmentWithinMtu() {
  auto archive = LargePayloadArchive();
  TEST_ASSERT_TRUE(MakeTelemetryPayloads(archive, 0).empty());
  TEST_ASSERT_TRUE(MakeTelemetryPayloads(archive, 1).empty());
  for (auto mtu : {512u, 1200u, 100000u}) {
    auto parts = MakeTelemetryPayloads(archive, mtu);
    TEST_ASSERT_FALSE(parts.empty());
    if (mtu <= 1200) {
      TEST_ASSERT_TRUE(parts.size() > 1);
    }
    for (auto const& part : parts) {
      TEST_ASSERT_TRUE(PacketSize(part) <= mtu);
      TEST_ASSERT_TRUE(part.cpp.compiler.empty());
      TEST_ASSERT_TRUE(part.cpp.os.empty());
    }
    auto restored = ReassembleTelemetryPayloads(parts);
    TEST_ASSERT_TRUE(restored.has_value());
    AssertArchiveEqual(archive, *restored);
    // Telemetric's constant discriminator deliberately prevents assignment.
    std::vector<Telemetric> reversed;
    for (auto i = parts.rbegin(); i != parts.rend(); ++i) {
      reversed.push_back(*i);
    }
    reversed.push_back(parts.front());
    restored = ReassembleTelemetryPayloads(reversed);
    TEST_ASSERT_TRUE(restored.has_value());
    AssertArchiveEqual(archive, *restored);
  }
}
void test_FragmentAssemblyRejectsGapsConflictsAndMixedTransfers() {
  auto archive = LargePayloadArchive();
  auto parts = MakeTelemetryPayloads(archive, 512);
  TEST_ASSERT_TRUE(parts.size() > 2);
  TEST_ASSERT_FALSE(
      ReassembleTelemetryPayloads(
          std::span<Telemetric const>{parts.data(), parts.size() - 1})
          .has_value());
  auto changed = DecodeTelemetryFragment(parts.front());
  TEST_ASSERT_TRUE(changed.has_value());
  changed->bytes[0] ^= 1;
  auto conflicting = parts;
  conflicting.emplace_back(EncodeFragment(*changed));
  TEST_ASSERT_FALSE(ReassembleTelemetryPayloads(conflicting).has_value());
  auto mixed = parts;
  auto other = MakeTelemetryPayloads(archive, 512);
  mixed.push_back(other.front());
  TEST_ASSERT_FALSE(ReassembleTelemetryPayloads(mixed).has_value());
  changed = DecodeTelemetryFragment(parts.front());
  changed->total = std::numeric_limits<std::uint64_t>::max();
  changed->offset = changed->total;
  TEST_ASSERT_FALSE(
      DecodeTelemetryFragment(EncodeFragment(*changed)).has_value());
  auto trailing = parts.front();
  trailing.cpp.blob.push_back(0);
  TEST_ASSERT_FALSE(DecodeTelemetryFragment(trailing).has_value());
  changed = DecodeTelemetryFragment(parts.front());
  changed->offset = 1;
  std::vector<Telemetric> shifted;
  shifted.emplace_back(EncodeFragment(*changed));
  for (std::size_t i = 1; i < parts.size(); ++i) {
    shifted.push_back(parts[i]);
  }
  TEST_ASSERT_FALSE(ReassembleTelemetryPayloads(shifted).has_value());
}
#if AE_DISTILLATION && AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
class SharedRamStorage final : public RamDomainStorage {
 public:
  explicit SharedRamStorage(std::shared_ptr<ObjClassData> data)
      : data_{std::move(data)} {
    state = *data_;
  }
  ~SharedRamStorage() override { *data_ = std::move(state); }

 private:
  std::shared_ptr<ObjClassData> data_;
};
std::unique_ptr<AetherApp> TestApp(
    std::shared_ptr<RamDomainStorage::ObjClassData> const& data,
    SharedRamStorage** live_storage = nullptr) {
  auto context = AetherAppContext{[data, live_storage]() {
    auto storage = std::make_unique<SharedRamStorage>(data);
    if (live_storage != nullptr) {
      *live_storage = storage.get();
    }
    return storage;
  }};
  auto configured =
      std::move(context)
          .AdaptersFactory([](AetherAppContext const& context) {
            auto existing =
                AdapterRegistry::ptr{context.aether()->adapter_registry};
            if (existing.is_valid()) {
              return existing;
            }
            return AdapterRegistry::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    GlobalId::kAdapterRegistry));
          })
          .CryptoFactory([](auto const&) { return Crypto::ptr{}; })
          .PollerFactory([](auto const&) { return IPoller::ptr{}; });
#  if AE_SUPPORT_REGISTRATION
  static_cast<void>(
      std::move(configured).RegistrationCloudFactory([](auto const&) {
        return Cloud::ptr{};
      }));
#  endif
#  if AE_SUPPORT_CLOUD_DNS
  static_cast<void>(std::move(configured).DnsResolverFactory([](auto const&) {
    return DnsResolver::ptr{};
  }));
#  endif
  return AetherApp::Construct(std::move(configured));
}
inline constexpr tele::Module kProbeModule{201, 1, 100, "persistence-test"};
inline constexpr tele::Tag kProbeTag{7, kProbeModule, "persistence-probe"};
void EmitAppProbe() {
  AE_TELE_INFO(kProbeTag, "app telemetry integration probe");
}
std::uint64_t ProbeCount(TeleStatisticsBackend& backend) {
  auto bytes = backend.Snapshot("aether");
  auto decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  std::optional<std::uint32_t> index;
  for (auto const& record : decoded.records) {
    std::string_view text{reinterpret_cast<char const*>(record.payload.data()),
                          record.payload.size()};
    if (text == "app telemetry integration probe") {
      index = record.index;
    }
  }
  TEST_ASSERT_TRUE(index.has_value());
  return tele::raw::RestoreMetric(decoded.records, *index).count;
}
void AssertInitialCheckpoint(SharedRamStorage const& storage, unsigned boot,
                             std::uint32_t started) {
  RamDomainStorage durable_copy;
  durable_copy.state = storage.state;
  Domain domain{durable_copy};
  auto statistics = TeleStatistics::ptr::Declare(
      CreateWith{domain}.with_id(GlobalId::kTeleStatistics));
  TEST_ASSERT_TRUE(statistics.Load());
  auto backend = statistics->backend();
  auto bytes = backend->Snapshot("aether");
  TEST_ASSERT_FALSE(bytes.empty());
  auto decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  bool found{};
  for (auto const& record : decoded.records) {
    found = found ||
            (record.kind == tele::raw::Kind::kLog && record.index == started);
  }
  TEST_ASSERT_TRUE(found);
#  if AE_FILTRATION
  TEST_ASSERT_TRUE(tele::raw::RestoreMetric(decoded.records, started).count ==
                   boot + 1);
#  else
  static_cast<void>(boot);
  TEST_ASSERT_TRUE(tele::raw::RestoreMetric(decoded.records, started).count ==
                   1);
#  endif
#  if AE_OBJECTS_RAW_TELE && !defined(NDEBUG) && !AE_NO_DEBUG_LOG
  TEST_ASSERT_FALSE(backend->Snapshot("objects").empty());
#  endif
}
void test_AetherAppLifecycleBindsBothNamespacesAndContinuesWithoutReplay() {
  auto data = std::make_shared<RamDomainStorage::ObjClassData>();
  for (unsigned boot = 0; boot < 3; ++boot) {
    {
      SharedRamStorage* live_storage{};
      auto app = TestApp(data, &live_storage);
      TEST_ASSERT_NOT_NULL(live_storage);
      auto backend = app->aether()->tele_statistics->backend();
      // AetherStarted is the first ordinary Aether record, before factory and
      // registry events. Keep this check usable when debug maps are compiled
      // out.
      auto live_bytes = backend->Snapshot("aether");
      auto live = tele::raw::Decode(live_bytes);
      std::optional<std::uint32_t> started;
      for (auto const& record : live.records) {
        if (record.kind == tele::raw::Kind::kLog) {
          started = record.index;
          break;
        }
      }
      TEST_ASSERT_TRUE(started.has_value());
      AssertInitialCheckpoint(*live_storage, boot, *started);
      auto const live_metric = AetherTeleSink::Instance().MetricFor(*started);
#  if AE_FILTRATION
      TEST_ASSERT_TRUE(live_metric.count == boot + 1);
      TEST_ASSERT_TRUE(live_metric.samples == boot + 1);
#  else
      TEST_ASSERT_TRUE(live_metric.count == 1);
      TEST_ASSERT_TRUE(live_metric.samples == 1);
#  endif
      TEST_ASSERT_TRUE(backend == AetherTeleSink::Instance().GetBackend());
#  if AE_OBJECTS_RAW_TELE
      TEST_ASSERT_TRUE(backend ==
                       objects_tele::ObjectTeleSink::Instance().GetBackend());
#  endif
      EmitAppProbe();
      TEST_ASSERT_TRUE(DrainAetherTelemetry());
#  if AE_FILTRATION
      TEST_ASSERT_TRUE(ProbeCount(*backend) == boot + 1);
#  else
      TEST_ASSERT_TRUE(ProbeCount(*backend) == 1);
#  endif
#  if AE_OBJECTS_RAW_TELE && !defined(NDEBUG) && !AE_NO_DEBUG_LOG
      TEST_ASSERT_FALSE(backend->Snapshot("objects").empty());
#  endif
      app->aether().Save();
    }
    TEST_ASSERT_TRUE(
        AetherTeleSink::Instance().GetBackend()->Snapshot("aether").empty());
  }
}
class GatedBackend final : public tele::raw::IBackend {
 public:
  explicit GatedBackend(std::shared_ptr<TeleStatisticsBackend> target)
      : target_{std::move(target)} {}
  bool Append(std::string_view space,
              std::span<std::uint8_t const> bytes) override {
    return !fail && target_->Append(space, bytes);
  }
  bool RestoreMetrics(std::string_view space, std::uint32_t writer,
                      std::span<tele::raw::MetricTotal> output,
                      std::size_t& count) override {
    return target_->RestoreMetrics(space, writer, output, count);
  }
  bool fail{};

 private:
  std::shared_ptr<TeleStatisticsBackend> target_;
};
void test_AetherCheckpointReportsFailedDrainAndRetryDoesNotDoubleCount() {
  auto data = std::make_shared<RamDomainStorage::ObjClassData>();
  auto app = TestApp(data);
  auto backend = app->aether()->tele_statistics->backend();
  auto gated = std::make_shared<GatedBackend>(backend);
  auto& sink = AetherTeleSink::Instance();
  TEST_ASSERT_TRUE(sink.SetBackend(gated));
  TEST_ASSERT_TRUE(sink.BindWriter(0));
  EmitAppProbe();
  gated->fail = true;
  TEST_ASSERT_FALSE(DrainAetherTelemetry());
  gated->fail = false;
  TEST_ASSERT_TRUE(DrainAetherTelemetry());
  TEST_ASSERT_TRUE(ProbeCount(*backend) == 1);
  TEST_ASSERT_TRUE(DrainAetherTelemetry());
  TEST_ASSERT_TRUE(ProbeCount(*backend) == 1);
  TEST_ASSERT_TRUE(sink.SetBackend(backend));
  TEST_ASSERT_TRUE(sink.BindWriter(0));
}
#endif
#if AE_ENABLE_PING
class NullConnectionFactory final : public IServerConnectionFactory {
 public:
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const&) override {
    return {};
  }
};
void test_PingAckCompletesOnlyMatchingPendingConnectionAndCancelsTimeout() {
  AeEnv environment;
  AeContext context{environment};
  NullConnectionFactory factory;
  CloudServerConnection connection{Ptr<Cloud>{}, ServerId{7}, factory};
  ClientApiSafe matching{environment.event_system()},
      unrelated{environment.event_system()};
  auto const start = Now();
  unsigned calls{};
  bool succeeded{};
  {
    Ping ping{context, connection, std::chrono::seconds{5},
              std::chrono::seconds{5}, std::chrono::seconds{1}};
    Subscription result{
        ping.result_event().Subscribe([&](Ping::PingResult const& value) {
          ++calls;
          succeeded = std::holds_alternative<Ok<Duration>>(value);
        })};
    PingTestAccess::Arm(ping, matching, start);
    TEST_ASSERT_TRUE(PingTestAccess::Waiting(ping));
    unrelated.PingAck();
    TEST_ASSERT_EQUAL_UINT(0, calls);
    matching.PingAck();
    TEST_ASSERT_EQUAL_UINT(0, calls);
    environment.scheduler().Update(start);
    TEST_ASSERT_EQUAL_UINT(1, calls);
    TEST_ASSERT_TRUE(succeeded);
    TEST_ASSERT_FALSE(PingTestAccess::Waiting(ping));
    matching.PingAck();
    environment.scheduler().Update(start + std::chrono::seconds{2});
    TEST_ASSERT_EQUAL_UINT(1, calls);
  }
  unsigned timeouts{};
  unsigned late_responses{};
  {
    Ping ping{context, connection, std::chrono::seconds{5},
              std::chrono::seconds{5}, std::chrono::seconds{1}};
    Subscription result{
        ping.result_event().Subscribe([&](Ping::PingResult const& value) {
          if (std::holds_alternative<Error<int>>(value)) {
            ++timeouts;
          } else if (std::holds_alternative<Ping::LateDuration>(value)) {
            ++late_responses;
          }
        })};
    PingTestAccess::Arm(ping, matching, start);
    environment.scheduler().Update(start + std::chrono::seconds{2});
    TEST_ASSERT_EQUAL_UINT(1, timeouts);
    TEST_ASSERT_TRUE(PingTestAccess::Waiting(ping));
    TEST_ASSERT_EQUAL_UINT(0, late_responses);
    matching.PingAck();
    TEST_ASSERT_EQUAL_UINT(0, late_responses);
    environment.scheduler().Update(start + std::chrono::seconds{2});
    TEST_ASSERT_FALSE(PingTestAccess::Waiting(ping));
    TEST_ASSERT_EQUAL_UINT(1, late_responses);
    matching.PingAck();
    environment.scheduler().Update(start + std::chrono::seconds{3});
    TEST_ASSERT_EQUAL_UINT(1, timeouts);
    TEST_ASSERT_EQUAL_UINT(1, late_responses);
  }
  {
    Ping ping{context, connection, std::chrono::seconds{5},
              std::chrono::seconds{5}, std::chrono::seconds{1}};
    PingTestAccess::Arm(ping, matching, start);
  }
  matching.PingAck();
  environment.scheduler().Update(start + std::chrono::seconds{4});
  TEST_ASSERT_EQUAL_UINT(1, timeouts);
  TEST_ASSERT_EQUAL_UINT(1, late_responses);
  TEST_ASSERT_EQUAL_UINT(1, calls);
}
#endif
}  // namespace ae::test_tele_persistence

void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_tele_persistence::
               test_SeveralRestartsContinueOnlyAcknowledgedDeltas);
  RUN_TEST(ae::test_tele_persistence::
               test_EarlyMergeIsIdempotentAndForwardsLateScopeDeltas);
  RUN_TEST(ae::test_tele_persistence::
               test_InvalidArchivesAndFailedAppendsDoNotReplaceHistory);
  RUN_TEST(
      ae::test_tele_persistence::test_ObjDomainSaveLoadContinuesWithoutStop);
  RUN_TEST(
      ae::test_tele_persistence::test_NamespacesAndWritersRemainIndependent);
  RUN_TEST(ae::test_tele_persistence::
               test_ObjectRestartAndDrainStartIndependentTimeAnchors);
  RUN_TEST(ae::test_tele_persistence::
               test_ArchiveLargerThanDefaultContainerGuardLoadsInChunks);
  RUN_TEST(ae::test_tele_persistence::
               test_TransportFragmentsHandleLargeRowsAndEnvironmentWithinMtu);
  RUN_TEST(
      ae::test_tele_persistence::
          test_CatalogChangesArchiveHistoryWithoutMisattributingNewIndices);
  RUN_TEST(ae::test_tele_persistence::
               test_TruncatedOrMalformedArchiveLoadLeavesDestinationUnchanged);
  RUN_TEST(ae::test_tele_persistence::
               test_FragmentAssemblyRejectsGapsConflictsAndMixedTransfers);
#if AE_DISTILLATION && AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  RUN_TEST(
      ae::test_tele_persistence::
          test_AetherAppLifecycleBindsBothNamespacesAndContinuesWithoutReplay);
  RUN_TEST(
      ae::test_tele_persistence::
          test_AetherCheckpointReportsFailedDrainAndRetryDoesNotDoubleCount);
#endif
  RUN_TEST(
      ae::test_tele_persistence::
          test_ConcurrentAppendSnapshotAndEarlyMergePreserveEveryWriterDelta);
#if AE_ENABLE_PING
  RUN_TEST(
      ae::test_tele_persistence::
          test_PingAckCompletesOnlyMatchingPendingConnectionAndCancelsTimeout);
#endif
  return UNITY_END();
}
