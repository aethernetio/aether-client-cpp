// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/log.h"
#include "aether-objects/obj/domain.h"
#include "aether-tele/raw/decode.h"
#include "aether-tele/raw/wire.h"
#include "aether/aether_app.h"
#include "aether/aether_tele.h"
#include "aether/global_ids.h"
#include "aether/tele_statistics.h"
#include "aether/telemetry_delivery.h"
#include "aether/telemetry_persistent_state.h"
namespace ae::test_telemetry_state {
struct BusinessState {
  std::string document;
  std::uint32_t revision{};
  AE_REFLECT_MEMBERS(document, revision)
};
DataBuffer Block(std::uint8_t kind, std::uint32_t fields, std::uint32_t writer,
                 std::span<std::uint8_t const> body) {
  std::array<std::uint8_t, tele::raw::wire::kMaxBlockHeaderBytes> header{};
  std::size_t size{};
  TEST_ASSERT_TRUE(tele::raw::wire::WriteBlockHeader(header, size, kind, fields,
                                                     writer, body.size(), 1));
  DataBuffer result(header.begin(), header.begin() + size);
  result.insert(result.end(), body.begin(), body.end());
  return result;
}
DataBuffer MetricBlock(std::uint32_t writer, std::uint32_t tag,
                       tele::raw::Metric metric) {
  std::array<std::uint8_t, 128> body{};
  std::size_t size{};
  TEST_ASSERT_TRUE(
      tele::raw::wire::Write<tele::raw::wire::TagCodec>(tag, body, size));
  for (auto value :
       {metric.count, metric.samples, metric.sum, metric.min, metric.max}) {
    TEST_ASSERT_TRUE(tele::raw::wire::Write<tele::raw::wire::MetricCodec>(
        value, body, size));
  }
  return Block(tele::raw::wire::kMetricRecord, 0, writer,
               std::span{body}.first(size));
}
DataBuffer LogBlock(std::uint32_t writer, std::uint32_t tag,
                    std::string_view text) {
  std::array<std::uint8_t, 128> body{};
  std::size_t size{};
  TEST_ASSERT_TRUE(
      tele::raw::wire::Write<tele::raw::wire::TagCodec>(tag, body, size));
  TEST_ASSERT_TRUE(tele::raw::wire::Write<tele::raw::wire::BlobCodec>(
      text.size(), body, size));
  std::copy(text.begin(), text.end(), body.begin() + size);
  size += text.size();
  return Block(tele::raw::wire::kLogRecord, tele::raw::kPayload, writer,
               std::span{body}.first(size));
}
void Append(DataBuffer& target, DataBuffer const& bytes) {
  target.insert(target.end(), bytes.begin(), bytes.end());
}
DataBuffer Prefix() {
  auto result = LogBlock(2, 17, "accepted log");
  Append(result, MetricBlock(2, 7, {2, 2, 30, 10, 20}));
  Append(result, MetricBlock(47, 7, {3, 1, 4, 4, 4}));
  return result;
}
DataBuffer Tail() {
  auto result = LogBlock(2, 19, "still unsent");
  Append(result, MetricBlock(2, 7, {1, 1, 5, 5, 5}));
  return result;
}
TelemetryBatch Batch() {
  return {NewTelemetryBatchId(), "apptraverse-app",
          EncodeTelemetryArtifact("{}", Prefix())};
}
TelemetryPersistentState State() {
  TeleStatisticsBackend backend{false};
  TEST_ASSERT_TRUE(backend.Append("aether", Prefix()));
  TelemetryPersistentState result;
  result.compatibility = TelemetryCompatibilityId();
  result.archive = backend.Snapshot();
  result.outbox.pending.push_back(Batch());
  result.outbox.capture_offsets["aether"] = Prefix().size();
  return result;
}
void WriteEnvelope(seri::BinaryArchive<seri::BinaryVectorBuffer<>>& archive,
                   std::string const& compatibility,
                   std::span<std::uint8_t const> body) {
  TEST_ASSERT_TRUE(archive.Save(compatibility).IsOk());
  constexpr std::size_t kChunk = 64 * 1024;
  TEST_ASSERT_TRUE(
      archive.buffer()
          .Write(seri::SizeWriteTag{(body.size() + kChunk - 1) / kChunk})
          .IsOk());
  for (std::size_t offset = 0; offset < body.size();) {
    auto size = std::min(kChunk, body.size() - offset);
    TEST_ASSERT_TRUE(archive.buffer().Write(seri::SizeWriteTag{size}).IsOk());
    TEST_ASSERT_TRUE(archive.buffer()
                         .Write(seri::DataWriteTag{body.data() + offset, size})
                         .IsOk());
    offset += size;
  }
}
void AssertMetric(tele::raw::Metric const& expected,
                  tele::raw::Metric const& actual) {
  TEST_ASSERT_TRUE(expected.count == actual.count &&
                   expected.samples == actual.samples &&
                   expected.sum == actual.sum && expected.min == actual.min &&
                   expected.max == actual.max);
}
auto Totals(std::span<std::uint8_t const> bytes) {
  auto decoded = tele::raw::Decode(bytes);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  std::map<std::pair<std::uint32_t, std::uint32_t>, tele::raw::Metric> result;
  for (auto const& record : decoded.records) {
    if (record.metric) {
      tele::raw::AccumulateMetric(result[{record.stream, record.index}],
                                  *record.metric);
    }
  }
  return result;
}
void test_IncompatibleMalformedBodyIsSkippedBeforeDecodeAndKeepsBusinessData() {
  DataBuffer bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  BusinessState const before{"apptraverse document", 41};
  BusinessState const after{"aeroadmin conversation", 73};
  TEST_ASSERT_TRUE(save.Save(before).IsOk());
  // If interpreted as an inner archive this starts with an absurd map count.
  // Cross a chunk boundary so the incompatible-body discard loop is exercised.
  DataBuffer malformed(70000, 0xff);
  WriteEnvelope(save, "old-incompatible-thread-layout", malformed);
  TEST_ASSERT_TRUE(save.Save(after).IsOk());

  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  BusinessState actual_before, actual_after;
  auto telemetry = State();
  TEST_ASSERT_TRUE(load.Load(actual_before).IsOk());
  TEST_ASSERT_TRUE(load.Load(telemetry).IsOk());
  TEST_ASSERT_TRUE(load.Load(actual_after).IsOk());
  TEST_ASSERT_TRUE(telemetry.reset);
  TEST_ASSERT_TRUE(telemetry.archive.spaces.empty());
  TEST_ASSERT_TRUE(telemetry.outbox.pending.empty());
  TEST_ASSERT_TRUE(telemetry.outbox.capture_offsets.empty());
  TEST_ASSERT_TRUE(actual_before.document == before.document &&
                   actual_before.revision == before.revision);
  TEST_ASSERT_TRUE(actual_after.document == after.document &&
                   actual_after.revision == after.revision);
  TEST_ASSERT_TRUE(load.buffer().read_position() == bytes.size());
}
void test_CompatibleMalformedBodyIsRejectedWithoutReplacingLiveState() {
  auto destination = State();
  auto expected_archive = destination.archive.spaces["aether"].blocks;
  auto expected_batch = destination.outbox.pending.front();
  auto expected_cursors = destination.outbox.capture_offsets;
  DataBuffer bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  std::array<std::uint8_t, 4> malformed{0xff, 0xff, 0xff, 0xff};
  WriteEnvelope(save, TelemetryCompatibilityId(), malformed);
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(load.Load(destination).IsOk());
  TEST_ASSERT_FALSE(destination.reset);
  TEST_ASSERT_TRUE(destination.compatibility == TelemetryCompatibilityId());
  TEST_ASSERT_TRUE(destination.archive.spaces["aether"].blocks ==
                   expected_archive);
  TEST_ASSERT_TRUE(destination.outbox.capture_offsets == expected_cursors);
  TEST_ASSERT_TRUE(destination.outbox.pending.size() == 1);
  TEST_ASSERT_TRUE(destination.outbox.pending.front().id == expected_batch.id);
  TEST_ASSERT_TRUE(destination.outbox.pending.front().artifact ==
                   expected_batch.artifact);
}
void test_CompatibleStatePreservesPendingImmutableBatchAndCursors() {
  auto original = State();
  DataBuffer bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save.Save(original).IsOk());
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetryPersistentState restored;
  TEST_ASSERT_TRUE(load.Load(restored).IsOk());
  TEST_ASSERT_FALSE(restored.reset);
  TEST_ASSERT_TRUE(restored.archive.spaces["aether"].blocks ==
                   original.archive.spaces["aether"].blocks);
  TEST_ASSERT_TRUE(restored.outbox.pending.size() == 1);
  TEST_ASSERT_TRUE(restored.outbox.pending.front().id ==
                   original.outbox.pending.front().id);
  TEST_ASSERT_TRUE(restored.outbox.pending.front().artifact ==
                   original.outbox.pending.front().artifact);
  TEST_ASSERT_TRUE(restored.outbox.capture_offsets ==
                   original.outbox.capture_offsets);
}
void test_CompactionPreservesEveryWriterMetricAndTheExactUnsentTail() {
  TeleStatisticsBackend backend{false};
  auto prefix = Prefix();
  auto tail = Tail();
  TEST_ASSERT_TRUE(backend.Append("aether", prefix));
  TEST_ASSERT_TRUE(backend.Append("aether", tail));
  auto before = backend.Snapshot("aether");
  auto expected = Totals(before);
  TelemetryOutbox outbox;
  outbox.capture_offsets["aether"] = prefix.size();
  TEST_ASSERT_TRUE(backend.CompactAcknowledged(outbox));
  auto after = backend.Snapshot("aether");
  auto checkpoint = outbox.capture_offsets["aether"];
  TEST_ASSERT_TRUE(after.size() < before.size());
  TEST_ASSERT_TRUE(checkpoint > 0 && checkpoint + tail.size() == after.size());
  TEST_ASSERT_TRUE(
      std::equal(tail.begin(), tail.end(), after.begin() + checkpoint));
  auto actual = Totals(after);
  TEST_ASSERT_TRUE(actual.size() == expected.size());
  for (auto const& [key, metric] : expected) {
    TEST_ASSERT_TRUE(actual.contains(key));
    AssertMetric(metric, actual.at(key));
  }
  AssertMetric({3, 3, 35, 5, 20}, actual.at({2, 7}));
  AssertMetric({3, 1, 4, 4, 4}, actual.at({47, 7}));
  auto decoded = tele::raw::Decode(after);
  unsigned logs{};
  for (auto const& record : decoded.records) {
    if (record.kind == tele::raw::Kind::kLog) {
      TEST_ASSERT_TRUE(record.index == 19);
      ++logs;
    }
  }
  TEST_ASSERT_TRUE(logs == 1);
  auto cursors = outbox.capture_offsets;
  TEST_ASSERT_TRUE(backend.CompactAcknowledged(outbox));
  TEST_ASSERT_TRUE(backend.Snapshot("aether") == after);
  TEST_ASSERT_TRUE(outbox.capture_offsets == cursors);
}
void test_CompactionRefusesPendingBatchAndInvalidCursorWithoutMutation() {
  TeleStatisticsBackend backend{false};
  TEST_ASSERT_TRUE(backend.Append("aether", Prefix()));
  auto before = backend.Snapshot("aether");
  TelemetryOutbox outbox;
  outbox.pending.push_back(Batch());
  outbox.capture_offsets["aether"] = before.size();
  TEST_ASSERT_FALSE(backend.CompactAcknowledged(outbox));
  TEST_ASSERT_TRUE(backend.Snapshot("aether") == before);
  TEST_ASSERT_TRUE(outbox.capture_offsets["aether"] == before.size());
  TEST_ASSERT_TRUE(outbox.pending.size() == 1);
  outbox.pending.clear();
  outbox.capture_offsets["aether"] = before.size() + 1;
  TEST_ASSERT_FALSE(backend.CompactAcknowledged(outbox));
  TEST_ASSERT_TRUE(backend.Snapshot("aether") == before);
  TEST_ASSERT_TRUE(outbox.capture_offsets["aether"] == before.size() + 1);
}
TelemetryProductInfo Product(char revision) {
  TelemetryProductInfo result;
  result.product = "apptraverse-app";
  result.commit = std::string(40, revision);
  result.build_id = std::string(64, revision);
  auto catalog = std::string(64, revision == 'a' ? 'c' : 'd');
  result.manifest_json =
      "{\"artifact_version\":1,\"wire_layout\":\"thread_blocks\","
      "\"product\":\"apptraverse-app\",\"build\":{\"build_id\":\"" +
      result.build_id + "\",\"repositories\":{\"sdk\":{\"commit\":\"" +
      result.commit + "\",\"dirty\":false,\"tree_sha256\":\"" +
      std::string(64, 'e') +
      "\"}}},\"namespaces\":[{\"name\":\"aether\",\"repository\":\"sdk\","
      "\"catalog_sha256\":\"" +
      catalog + "\"}]}";
  return result;
}
void Configure(TelemetryProductInfo const& info) {
  ConfigureProductTelemetry(info.product, info.commit, info.build_id,
                            info.manifest_json);
}
DataBuffer UnvalidatedState(TelemetryPersistentState const& state) {
  DataBuffer body;
  auto inner = seri::BinaryArchive{seri::BinaryVectorBuffer<>{body}};
  TEST_ASSERT_TRUE(inner.Save(state.archive).IsOk());
  TEST_ASSERT_TRUE(inner.Save(state.outbox).IsOk());
  TEST_ASSERT_TRUE(inner.Save(state.source).IsOk());
  TEST_ASSERT_TRUE(
      inner.buffer().Write(seri::SizeWriteTag{state.epochs.size()}).IsOk());
  for (auto const& epoch : state.epochs) {
    TEST_ASSERT_TRUE(inner.Save(epoch.source).IsOk());
    TEST_ASSERT_TRUE(inner.Save(epoch.ends).IsOk());
  }
  DataBuffer result;
  auto outer = seri::BinaryArchive{seri::BinaryVectorBuffer<>{result}};
  WriteEnvelope(outer, state.compatibility, body);
  return result;
}
void test_MidBlockCursorsAndEpochEndsAndRegressingEpochsAreRejectedAtomically() {
  Configure(Product('a'));
  auto invalid = State();
  invalid.source = Product('a');
  auto destination = State();
  auto expected = destination.archive.spaces["aether"].blocks;
  auto expected_id = destination.outbox.pending.front().id;
  invalid.outbox.capture_offsets["aether"] = Prefix().size() - 1;
  auto bytes = UnvalidatedState(invalid);
  auto cursor_load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(cursor_load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.archive.spaces["aether"].blocks == expected);
  TEST_ASSERT_TRUE(destination.outbox.pending.front().id == expected_id);
  invalid.outbox.capture_offsets["aether"] = 0;
  invalid.epochs.push_back({Product('a'), {{"aether", Prefix().size() - 1}}});
  bytes = UnvalidatedState(invalid);
  auto epoch_load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(epoch_load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.archive.spaces["aether"].blocks == expected);
  invalid.epochs.clear();
  invalid.epochs.push_back({Product('a'), {{"aether", Prefix().size()}}});
  invalid.epochs.push_back(
      {Product('b'), {{"aether", LogBlock(2, 17, "accepted log").size()}}});
  bytes = UnvalidatedState(invalid);
  auto order_load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(order_load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.archive.spaces["aether"].blocks == expected);
  TEST_ASSERT_TRUE(destination.outbox.pending.front().id == expected_id);
}
void test_EpochRejectsOversizedNamespaceCountBeforeAllocatingTheMap() {
  DataBuffer bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save.Save(Product('a')).IsOk());
  TEST_ASSERT_TRUE(
      save.buffer()
          .Write(seri::SizeWriteTag{kTelemetryMaxCaptureNamespaces + 1})
          .IsOk());
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetrySourceEpoch destination{Product('b'), {{"aether", 0}}};
  TEST_ASSERT_FALSE(load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(load.buffer().read_position() == bytes.size());
  TEST_ASSERT_TRUE(destination.source.build_id == Product('b').build_id);
  TEST_ASSERT_TRUE(destination.ends.size() == 1);
}
void test_CompatibleStateCannotExceedDefaultBackendCapacityOrNamespaceLimit() {
  Configure(Product('a'));
  auto invalid = State();
  invalid.source = Product('a');
  auto block = MetricBlock(2, 7, {1, 0, 0, 0, 0});
  auto& bytes = invalid.archive.spaces["aether"].blocks;
  bytes.clear();
  while (bytes.size() <= 8 * 1024 * 1024) {
    Append(bytes, block);
  }
  invalid.outbox.capture_offsets.clear();
  auto serialized = UnvalidatedState(invalid);
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{serialized}};
  auto destination = State();
  auto expected_id = destination.outbox.pending.front().id;
  TEST_ASSERT_FALSE(load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.outbox.pending.front().id == expected_id);
  invalid.archive.spaces.clear();
  for (unsigned i = 0; i <= kTelemetryMaxCaptureNamespaces; ++i) {
    invalid.archive.spaces["space" + std::to_string(i)] = {};
  }
  serialized = UnvalidatedState(invalid);
  auto names_load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{serialized}};
  TEST_ASSERT_FALSE(names_load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.outbox.pending.front().id == expected_id);
}
void test_CompatibleBuildChangeKeepsSchemaGateAndSourceMetadata() {
  auto old_product = Product('a');
  Configure(old_product);
  auto original = State();
  original.source = old_product;
  DataBuffer bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save.Save(original).IsOk());
  auto new_product = Product('b');
  Configure(new_product);
  TEST_ASSERT_TRUE(TelemetryCompatibilityId() == original.compatibility);
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetryPersistentState restored;
  TEST_ASSERT_TRUE(load.Load(restored).IsOk());
  TEST_ASSERT_FALSE(restored.reset);
  TEST_ASSERT_TRUE(restored.source.build_id == old_product.build_id);
  TEST_ASSERT_TRUE(restored.source.manifest_json == old_product.manifest_json);
  TEST_ASSERT_TRUE(restored.outbox.pending.front().artifact ==
                   original.outbox.pending.front().artifact);
}
#if AE_DISTILLATION && AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
std::string_view Manifest(DataBuffer const& artifact) {
  TEST_ASSERT_TRUE(ValidTelemetryArtifact(artifact));
  auto length = static_cast<std::size_t>(artifact[8]) |
                (static_cast<std::size_t>(artifact[9]) << 8) |
                (static_cast<std::size_t>(artifact[10]) << 16) |
                (static_cast<std::size_t>(artifact[11]) << 24);
  return {reinterpret_cast<char const*>(artifact.data() + 12), length};
}
void AssertPayload(DataBuffer const& artifact, DataBuffer const& expected) {
  auto manifest = Manifest(artifact);
  auto offset = 12 + manifest.size();
  TEST_ASSERT_TRUE(artifact.size() == offset + expected.size());
  TEST_ASSERT_TRUE(
      std::equal(expected.begin(), expected.end(), artifact.begin() + offset));
}
void test_CompatibleObjectUpgradeSealsOldBuildAndSeparatesNewCapture() {
  RamDomainStorage storage;
  auto old_product = Product('a');
  auto new_product = Product('b');
  auto prefix = Prefix();
  auto tail = Tail();
  Configure(old_product);
  {
    Domain domain{storage};
    auto statistics =
        TeleStatistics::ptr::Create(CreateWith{domain}.with_id(405));
    TEST_ASSERT_TRUE(bool(statistics.Load()));
    TEST_ASSERT_TRUE(statistics->backend()->Append("aether", prefix));
    DomainGraph graph{&domain};
    TEST_ASSERT_TRUE(graph.SaveRoot(statistics.Load(), statistics.id()).IsOk());
  }
  Configure(new_product);
  DataBuffer old_artifact, new_artifact;
  {
    Domain domain{storage};
    auto statistics =
        TeleStatistics::ptr::Declare(CreateWith{domain}.with_id(405));
    TEST_ASSERT_TRUE(bool(statistics.Load()));
    TEST_ASSERT_FALSE(statistics->telemetry_state_reset());
    TEST_ASSERT_TRUE(statistics->backend()->Append("aether", tail));
    TEST_ASSERT_TRUE(statistics->PrepareDelivery());
    auto& outbox = statistics->delivery_outbox();
    TEST_ASSERT_TRUE(outbox.pending.size() == 1);
    old_artifact = outbox.pending.front().artifact;
    TEST_ASSERT_TRUE(Manifest(old_artifact).find(old_product.build_id) !=
                     std::string_view::npos);
    TEST_ASSERT_TRUE(Manifest(old_artifact).find(std::string(64, 'c')) !=
                     std::string_view::npos);
    AssertPayload(old_artifact, prefix);
    TEST_ASSERT_TRUE(statistics->PrepareDelivery());
    TEST_ASSERT_TRUE(outbox.pending.size() == 2);
    new_artifact = outbox.pending.back().artifact;
    TEST_ASSERT_TRUE(Manifest(new_artifact).find(new_product.build_id) !=
                     std::string_view::npos);
    TEST_ASSERT_TRUE(Manifest(new_artifact).find(std::string(64, 'd')) !=
                     std::string_view::npos);
    AssertPayload(new_artifact, tail);
    TEST_ASSERT_TRUE(outbox.capture_offsets["aether"] ==
                     prefix.size() + tail.size());
    DomainGraph graph{&domain};
    TEST_ASSERT_TRUE(graph.SaveRoot(statistics.Load(), statistics.id()).IsOk());
  }
  // Another object lifetime still retries the identical old artifact.
  {
    Domain domain{storage};
    auto statistics =
        TeleStatistics::ptr::Declare(CreateWith{domain}.with_id(405));
    TEST_ASSERT_TRUE(bool(statistics.Load()));
    auto& outbox = statistics->delivery_outbox();
    TEST_ASSERT_TRUE(outbox.pending.size() == 2);
    TEST_ASSERT_TRUE(outbox.pending.front().artifact == old_artifact);
    TEST_ASSERT_TRUE(outbox.pending.back().artifact == new_artifact);
    TEST_ASSERT_TRUE(statistics->PrepareDelivery());
    TEST_ASSERT_TRUE(outbox.pending.size() == 2);
  }
}
void test_EpochOverflowResetsOnlyMutableTelemetryAndRetainsImmutableOutbox() {
  RamDomainStorage storage;
  auto immutable = Batch();
  for (unsigned boot = 0; boot <= 65; ++boot) {
    Configure(Product(boot % 2 == 0 ? 'a' : 'b'));
    Domain domain{storage};
    auto statistics =
        boot == 0
            ? TeleStatistics::ptr::Create(CreateWith{domain}.with_id(406))
            : TeleStatistics::ptr::Declare(CreateWith{domain}.with_id(406));
    TEST_ASSERT_TRUE(bool(statistics.Load()));
    auto& outbox = statistics->delivery_outbox();
    if (boot == 0) {
      outbox.pending.push_back(immutable);
    }
    TEST_ASSERT_TRUE(outbox.pending.size() == 1);
    TEST_ASSERT_TRUE(outbox.pending.front().id == immutable.id);
    TEST_ASSERT_TRUE(outbox.pending.front().artifact == immutable.artifact);
    if (boot == 65) {
      TEST_ASSERT_TRUE(statistics->telemetry_state_reset());
      TEST_ASSERT_TRUE(statistics->backend()->Snapshot().spaces.empty());
      TEST_ASSERT_TRUE(outbox.capture_offsets.empty());
    } else {
      TEST_ASSERT_FALSE(statistics->telemetry_state_reset());
    }
    TEST_ASSERT_TRUE(
        statistics->backend()->Append("aether", LogBlock(2, 17, "pending")));
    DomainGraph graph{&domain};
    TEST_ASSERT_TRUE(graph.SaveRoot(statistics.Load(), statistics.id()).IsOk());
  }
}
std::unique_ptr<AetherApp> LocalApp(bool owner,
                                    RamDomainStorage** storage = nullptr) {
  auto context = AetherAppContext{[storage] {
    auto memory = std::make_unique<RamDomainStorage>();
    if (storage != nullptr) {
      *storage = memory.get();
    }
    return memory;
  }};
  auto configured =
      std::move(context)
          .TelemetryOwner(owner)
          .AdaptersFactory([](AetherAppContext const& context) {
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
inline constexpr tele::Module kLifeModule{257, 1, 100,
                                          "test-telemetry-lifecycle"};
inline constexpr tele::Tag kLifeTag{1, kLifeModule, "lifecycle-probe"};
void LifecycleProbe(std::string_view text) {
  AETHER_TELE_INFO(kLifeTag, "{}", text);
}
bool ContainsLog(TeleStatisticsBackend& backend, std::string_view text) {
  auto bytes = backend.Snapshot("aether");
  auto decoded = tele::raw::Decode(bytes);
  if (decoded.status != tele::raw::DecodeStatus::kOk) {
    return false;
  }
  return std::any_of(
      decoded.records.begin(), decoded.records.end(), [&](auto const& record) {
        return record.kind == tele::raw::Kind::kLog &&
               std::string_view{
                   reinterpret_cast<char const*>(record.payload.data()),
                   record.payload.size()} == text;
      });
}
void test_ParticipantThreadTeardownLeavesOwnerLoggingAndPersisting() {
  Configure(Product('a'));
  RamDomainStorage* memory{};
  auto owner = LocalApp(true, &memory);
  TEST_ASSERT_TRUE(owner->OwnsTelemetry());
  TEST_ASSERT_NOT_NULL(memory);
  auto statistics = owner->aether()->tele_statistics;
  auto backend = statistics->backend();
  std::atomic<bool> participant_ok{false};
  std::thread participant{[&] {
    try {
      auto app = LocalApp(false);
      bool separate = !app->OwnsTelemetry() &&
                      app->aether()->tele_statistics->backend() != backend;
      LifecycleProbe("participant-lifecycle-probe");
      participant_ok = separate && DrainAetherTelemetry();
    } catch (...) {
      participant_ok = false;
    }
  }};
  participant.join();
  TEST_ASSERT_TRUE(participant_ok.load());
  TEST_ASSERT_TRUE(AetherTeleSink::Instance().GetBackend() == backend);
  LifecycleProbe("owner-after-participant");
  TEST_ASSERT_TRUE(DrainAetherTelemetry());
  TEST_ASSERT_TRUE(ContainsLog(*backend, "participant-lifecycle-probe"));
  TEST_ASSERT_TRUE(ContainsLog(*backend, "owner-after-participant"));
  DomainGraph graph{statistics->domain};
  TEST_ASSERT_TRUE(graph.SaveRoot(statistics.Load(), statistics.id()).IsOk());
  RamDomainStorage durable;
  durable.state = memory->state;
  Domain domain{durable};
  auto restored = TeleStatistics::ptr::Declare(
      CreateWith{domain}.with_id(GlobalId::kTeleStatistics));
  TEST_ASSERT_TRUE(bool(restored.Load()));
  TEST_ASSERT_TRUE(
      ContainsLog(*restored->backend(), "owner-after-participant"));
  TEST_ASSERT_TRUE(
      ContainsLog(*restored->backend(), "participant-lifecycle-probe"));
}
void test_ConcurrentSecondOwnerIsRejectedWithoutChangingTheFirstBackend() {
  Configure(Product('a'));
  auto owner = LocalApp(true);
  auto backend = owner->aether()->tele_statistics->backend();
  bool rejected = false;
  try {
    auto accidental_owner = LocalApp(true);
  } catch (std::logic_error const&) {
    rejected = true;
  }
  TEST_ASSERT_TRUE(rejected);
  TEST_ASSERT_TRUE(AetherTeleSink::Instance().GetBackend() == backend);
  LifecycleProbe("owner-after-rejected-second-owner");
  TEST_ASSERT_TRUE(DrainAetherTelemetry());
  TEST_ASSERT_TRUE(ContainsLog(*backend, "owner-after-rejected-second-owner"));
}
#  if AE_OBJECTS_RAW_TELE && !defined(NDEBUG) && !AE_NO_DEBUG_LOG
void test_ObjectCountOnlyEventDoesNotFormatLogOrMeasureDuration() {
  auto& sink = objects_tele::ObjectTeleSink::Instance();
  auto previous = sink.GetBackend();
  auto options = sink.Runtime();
  auto backend = std::make_shared<TeleStatisticsBackend>(false);
  TEST_ASSERT_TRUE(sink.SetBackend(backend));
  sink.SetRuntime({true, false, true, true, tele::raw::kAllFields});
  constexpr tele::Tag count_probe{60000, objects_tele::kModule,
                                  "CountOnlyRegressionProbe"};
  unsigned formatted_arguments{};
  {
    AE_LOG_COUNT_TAGGED(count_probe, "must not format {}",
                        ++formatted_arguments);
  }
  bool drained = sink.DrainAll();
  auto bytes = backend->Snapshot("objects");
  auto decoded = tele::raw::Decode(bytes);
  auto metric = tele::raw::RestoreMetric(decoded.records, count_probe.index());
  auto logs = std::count_if(
      decoded.records.begin(), decoded.records.end(),
      [](auto const& record) { return record.kind == tele::raw::Kind::kLog; });
  bool restored = sink.SetBackend(previous);
  sink.SetRuntime(options);
  TEST_ASSERT_TRUE(drained && restored);
  TEST_ASSERT_TRUE(decoded.status == tele::raw::DecodeStatus::kOk);
  TEST_ASSERT_TRUE(formatted_arguments == 0);
  TEST_ASSERT_TRUE(logs == 0);
  AssertMetric({1, 0, 0, 0, 0}, metric);
}
#  endif
#endif
}  // namespace ae::test_telemetry_state
void setUp() {}
void tearDown() {}
int test_telemetry_state() {
  UNITY_BEGIN();
  RUN_TEST(
      ae::test_telemetry_state::
          test_IncompatibleMalformedBodyIsSkippedBeforeDecodeAndKeepsBusinessData);
  RUN_TEST(ae::test_telemetry_state::
               test_CompatibleMalformedBodyIsRejectedWithoutReplacingLiveState);
  RUN_TEST(ae::test_telemetry_state::
               test_CompatibleStatePreservesPendingImmutableBatchAndCursors);
  RUN_TEST(ae::test_telemetry_state::
               test_CompactionPreservesEveryWriterMetricAndTheExactUnsentTail);
  RUN_TEST(
      ae::test_telemetry_state::
          test_CompactionRefusesPendingBatchAndInvalidCursorWithoutMutation);
  RUN_TEST(ae::test_telemetry_state::
               test_CompatibleBuildChangeKeepsSchemaGateAndSourceMetadata);
  RUN_TEST(
      ae::test_telemetry_state::
          test_MidBlockCursorsAndEpochEndsAndRegressingEpochsAreRejectedAtomically);
  RUN_TEST(ae::test_telemetry_state::
               test_EpochRejectsOversizedNamespaceCountBeforeAllocatingTheMap);
  RUN_TEST(
      ae::test_telemetry_state::
          test_CompatibleStateCannotExceedDefaultBackendCapacityOrNamespaceLimit);
#if AE_DISTILLATION && AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  RUN_TEST(ae::test_telemetry_state::
               test_CompatibleObjectUpgradeSealsOldBuildAndSeparatesNewCapture);
  RUN_TEST(
      ae::test_telemetry_state::
          test_EpochOverflowResetsOnlyMutableTelemetryAndRetainsImmutableOutbox);
  RUN_TEST(ae::test_telemetry_state::
               test_ParticipantThreadTeardownLeavesOwnerLoggingAndPersisting);
  RUN_TEST(
      ae::test_telemetry_state::
          test_ConcurrentSecondOwnerIsRejectedWithoutChangingTheFirstBackend);
#  if AE_OBJECTS_RAW_TELE && !defined(NDEBUG) && !AE_NO_DEBUG_LOG
  RUN_TEST(ae::test_telemetry_state::
               test_ObjectCountOnlyEventDoesNotFormatLogOrMeasureDuration);
#  endif
#endif
  return UNITY_END();
}
int main() { return test_telemetry_state(); }
