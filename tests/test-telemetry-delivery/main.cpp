// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include <unity.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include "aether/telemetry_delivery.h"
#include "aether/telemetry_keeper.h"
#include "real_delivery_fixture.h"
namespace ae::test_telemetry_delivery {
struct Temporary {
  Temporary()
      : path{std::filesystem::temp_directory_path() /
             ("aether-keeper-" + TelemetryBatchIdText(NewTelemetryBatchId()))} {
    std::filesystem::create_directories(path);
  }
  ~Temporary() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};
TelemetryBatch Batch(std::size_t payload_size = 12000) {
  std::vector<std::uint8_t> payload(payload_size, 0xa5);
  return {NewTelemetryBatchId(), "apptraverse-app",
          EncodeTelemetryArtifact("{\"test\":true}", payload)};
}
Uid Source() { return Uid::FromString("12345678-1234-1234-1234-123456789abc"); }
std::vector<DataBuffer> Frames(TelemetryBatch batch) {
  TelemetryOutbox outbox;
  outbox.pending.push_back(std::move(batch));
  TelemetryDeliveryPump pump{outbox, [] { return true; }};
  std::vector<DataBuffer> frames;
  while (auto frame = pump.Request(512)) {
    frames.push_back(std::move(*frame));
    pump.Written(true);
  }
  TEST_ASSERT_FALSE(frames.empty());
  TEST_ASSERT_EQUAL_UINT(1, outbox.pending.size());
  return frames;
}
void test_Sha256MatchesPublishedVectors() {
  TEST_ASSERT_TRUE(
      TelemetrySha256({}) ==
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  std::string abc = "abc";
  TEST_ASSERT_TRUE(
      TelemetrySha256(std::span{
          reinterpret_cast<std::uint8_t const*>(abc.data()), abc.size()}) ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  std::vector<std::uint8_t> million(1000000, 'a');
  TEST_ASSERT_TRUE(
      TelemetrySha256(million) ==
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
void test_OnlyDurableApplicationAckRemovesTheOutbox() {
  Temporary temporary;
  auto batch = Batch();
  TelemetryOutbox outbox;
  outbox.pending.push_back(batch);
  bool persist_ok = true;
  unsigned saves = 0;
  TelemetryDeliveryPump pump{outbox, [&] {
                               ++saves;
                               return persist_ok;
                             }};
  TelemetryKeeperStore store{temporary.path};
  TelemetryKeeperReceiver receiver{store};
  std::optional<DataBuffer> ack;
  while (auto frame = pump.Request(512)) {
    ack = receiver.Receive(Source(), *frame);
    pump.Written(true);
    TEST_ASSERT_EQUAL_UINT(1, outbox.pending.size());
  }
  TEST_ASSERT_TRUE(ack.has_value());
  TEST_ASSERT_TRUE(std::filesystem::exists(store.PathFor(Source(), batch)));
  persist_ok = false;
  TEST_ASSERT_FALSE(pump.Receive(*ack));
  TEST_ASSERT_EQUAL_UINT(1, outbox.pending.size());
  persist_ok = true;
  TEST_ASSERT_TRUE(pump.Receive(*ack));
  TEST_ASSERT_TRUE(outbox.pending.empty());
  TEST_ASSERT_TRUE(saves >= 3);
}
void test_ReceiverRestartAndLostAckDeduplicateWithoutRewriting() {
  Temporary temporary;
  auto batch = Batch();
  auto frames = Frames(batch);
  TelemetryKeeperStore store{temporary.path};
  {
    TelemetryKeeperReceiver receiver{store};
    for (std::size_t i = 0; i < frames.size() / 2; ++i) {
      TEST_ASSERT_FALSE(receiver.Receive(Source(), frames[i]).has_value());
    }
  }
  TEST_ASSERT_FALSE(std::filesystem::exists(store.PathFor(Source(), batch)));
  std::optional<DataBuffer> ack;
  {
    TelemetryKeeperReceiver receiver{store};
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
      auto reply = receiver.Receive(Source(), *it);
      if (reply) {
        ack = std::move(reply);
      }
    }
  }
  TEST_ASSERT_TRUE(ack.has_value());
  auto time = std::filesystem::last_write_time(store.PathFor(Source(), batch));
  TelemetryKeeperStore reopened{temporary.path};
  TelemetryKeeperReceiver receiver{reopened};
  ack.reset();
  for (auto const& frame : frames) {
    auto reply = receiver.Receive(Source(), frame);
    if (reply) {
      ack = std::move(reply);
    }
  }
  TEST_ASSERT_TRUE(ack.has_value());
  TEST_ASSERT_TRUE(
      std::filesystem::last_write_time(store.PathFor(Source(), batch)) == time);
  auto changed = batch;
  changed.artifact.back() ^= 1;
  TEST_ASSERT_TRUE(reopened.Store(Source(), changed) ==
                   TelemetryStoreResult::kRejected);
}
void test_ConflictingFragmentsAndStorageFailureAreNotAcked() {
  Temporary temporary;
  auto batch = Batch();
  auto frames = Frames(batch);
  TelemetryKeeperStore store{temporary.path};
  TelemetryKeeperReceiver receiver{store};
  TEST_ASSERT_FALSE(receiver.Receive(Source(), frames.front()).has_value());
  auto decoded = DecodeTelemetryDeliveryFrame(frames.front());
  TEST_ASSERT_TRUE(decoded.has_value());
  decoded->bytes.back() ^= 1;
  TEST_ASSERT_FALSE(
      receiver.Receive(Source(), EncodeTelemetryDeliveryFrame(*decoded))
          .has_value());
  TEST_ASSERT_FALSE(std::filesystem::exists(store.PathFor(Source(), batch)));
  auto blocked = temporary.path / "file";
  std::ofstream{blocked} << "blocked";
  TelemetryKeeperStore unavailable{blocked};
  TelemetryKeeperReceiver rejected{unavailable};
  for (auto const& frame : frames) {
    TEST_ASSERT_FALSE(rejected.Receive(Source(), frame).has_value());
  }
}
void test_PendingLargeBatchSurvivesOrdinarySerialization() {
  TelemetryOutbox original;
  original.pending.push_back(Batch(2 * 1024 * 1024));
  original.capture_offsets["objects"] = 123;
  std::vector<std::uint8_t> bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save.Save(original).IsOk());
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetryOutbox restored;
  TEST_ASSERT_TRUE(load.Load(restored).IsOk());
  TEST_ASSERT_TRUE(restored.pending.front().id == original.pending.front().id);
  TEST_ASSERT_TRUE(restored.pending.front().artifact ==
                   original.pending.front().artifact);
  TEST_ASSERT_TRUE(restored.capture_offsets == original.capture_offsets);
  TelemetryDeliveryPump pump{restored, [] { return true; }};
  auto first = pump.Request(512);
  TEST_ASSERT_TRUE(first.has_value());
  auto frame = DecodeTelemetryDeliveryFrame(*first);
  TEST_ASSERT_TRUE(frame.has_value());
  TEST_ASSERT_TRUE(frame->id == original.pending.front().id &&
                   frame->offset == 0);
}
void test_InvalidFrameAndWrongAckDoNotConsumeHistory() {
  auto batch = Batch();
  auto frames = Frames(batch);
  auto trailing = frames.front();
  trailing.push_back(0);
  TEST_ASSERT_FALSE(DecodeTelemetryDeliveryFrame(trailing).has_value());
  for (std::size_t cut = 0; cut < frames.front().size(); ++cut) {
    TEST_ASSERT_FALSE(
        DecodeTelemetryDeliveryFrame(std::span{frames.front()}.first(cut))
            .has_value());
  }
  TelemetryOutbox outbox;
  outbox.pending.push_back(batch);
  TelemetryDeliveryPump pump{outbox, [] { return true; }};
  TEST_ASSERT_TRUE(pump.Request(512).has_value());
  pump.Written(true);
  TelemetryDeliveryFrame ack;
  ack.kind = 1;
  ack.id = NewTelemetryBatchId();
  ack.product = batch.product;
  TEST_ASSERT_FALSE(pump.Receive(EncodeTelemetryDeliveryFrame(ack)));
  TEST_ASSERT_EQUAL_UINT(1, outbox.pending.size());
}
void test_PersistFailureAndWriteRetryKeepTheSameImmutableBatch() {
  TelemetryOutbox outbox;
  outbox.pending.push_back(Batch());
  auto expected = outbox.pending.front();
  unsigned mode{};
  TelemetryDeliveryPump pump{outbox, [&]() -> bool {
                               if (mode == 1) {
                                 throw std::runtime_error("storage failure");
                               }
                               return mode == 2;
                             }};
  TEST_ASSERT_FALSE(pump.Request(512).has_value());
  mode = 1;
  TEST_ASSERT_FALSE(pump.Request(512).has_value());
  mode = 2;
  auto first = pump.Request(512);
  TEST_ASSERT_TRUE(first.has_value());
  pump.Written(false);
  auto retry = pump.Request(512);
  TEST_ASSERT_TRUE(retry.has_value());
  TEST_ASSERT_TRUE(*retry == *first);
  pump.Written(true);
  pump.Retry();
  auto restart = pump.Request(512);
  TEST_ASSERT_TRUE(restart.has_value());
  TEST_ASSERT_TRUE(*restart == *first);
  TEST_ASSERT_TRUE(outbox.pending.front().artifact == expected.artifact);
  TEST_ASSERT_TRUE(outbox.pending.front().id == expected.id);
}
void test_OutboxRejectsUnboundedCountsAndAggregateBeforePayloadAllocation() {
  TelemetryOutbox destination;
  destination.pending.push_back(Batch());
  auto original = destination.pending.front();
  std::vector<std::uint8_t> bytes;
  auto save = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(
      save.buffer()
          .Write(seri::SizeWriteTag{kTelemetryMaxPendingBatches + 1})
          .IsOk());
  auto load_count = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(load_count.Load(destination).IsOk());
  TEST_ASSERT_TRUE(load_count.buffer().read_position() == bytes.size());
  TEST_ASSERT_TRUE(destination.pending.size() == 1 &&
                   destination.pending.front().id == original.id);

  bytes.clear();
  auto save_chunks = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save_chunks.buffer().Write(seri::SizeWriteTag{1}).IsOk());
  TEST_ASSERT_TRUE(save_chunks.Save(original.id).IsOk());
  TEST_ASSERT_TRUE(save_chunks.Save(original.product).IsOk());
  // A single claimed batch larger than the aggregate limit is rejected at its
  // chunk count; no chunk payload is present or allocated by the decoder.
  TEST_ASSERT_TRUE(
      save_chunks.buffer()
          .Write(seri::SizeWriteTag{kTelemetryMaxOutboxBytes / (64 * 1024) + 1})
          .IsOk());
  auto load_chunks = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(load_chunks.Load(destination).IsOk());
  TEST_ASSERT_TRUE(load_chunks.buffer().read_position() == bytes.size());
  TEST_ASSERT_TRUE(destination.pending.size() == 1 &&
                   destination.pending.front().artifact == original.artifact);

  bytes.clear();
  auto save_cursors = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_TRUE(save_cursors.buffer().Write(seri::SizeWriteTag{0}).IsOk());
  TEST_ASSERT_TRUE(
      save_cursors.buffer()
          .Write(seri::SizeWriteTag{kTelemetryMaxCaptureNamespaces + 1})
          .IsOk());
  auto load_cursors = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(load_cursors.Load(destination).IsOk());
  TEST_ASSERT_TRUE(load_cursors.buffer().read_position() == bytes.size());
  TEST_ASSERT_TRUE(destination.pending.front().id == original.id);
}
void test_OutboxRejectsDuplicateIdsInvalidProductsAndInvalidArtifacts() {
  TelemetryOutbox invalid;
  invalid.pending.push_back(Batch());
  invalid.pending.push_back(invalid.pending.front());
  std::vector<std::uint8_t> bytes;
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TEST_ASSERT_FALSE(archive.Save(invalid).IsOk());
  TEST_ASSERT_TRUE(bytes.empty());
  // Individual batches are valid, but a repeated id in a forged list is not.
  TEST_ASSERT_TRUE(archive.buffer().Write(seri::SizeWriteTag{2}).IsOk());
  TEST_ASSERT_TRUE(archive.Save(invalid.pending.front()).IsOk());
  TEST_ASSERT_TRUE(archive.Save(invalid.pending.front()).IsOk());
  auto load = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  TelemetryOutbox destination;
  TEST_ASSERT_FALSE(load.Load(destination).IsOk());
  TEST_ASSERT_TRUE(destination.pending.empty());
  invalid.pending.resize(1);
  bytes.clear();
  invalid.pending.front().id = {};
  TEST_ASSERT_FALSE(archive.Save(invalid).IsOk());
  invalid.pending.front().id = NewTelemetryBatchId();
  invalid.pending.front().product = "third-product";
  TEST_ASSERT_FALSE(archive.Save(invalid).IsOk());
  invalid.pending.front().product = "aeroadmin-chat";
  invalid.pending.front().artifact[0] = 0;
  TEST_ASSERT_FALSE(archive.Save(invalid).IsOk());
  TEST_ASSERT_TRUE(bytes.empty());
}
int ArtifactFixture(int argc, char** argv) {
  std::filesystem::path artifact_path, storage_path;
  std::string product;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") {
      std::cout << "test-telemetry-delivery --artifact FILE --storage DIR "
                   "--product apptraverse-app|aeroadmin-chat\n"
                   "Deterministic restart/lost-ACK fixture; no network.\n";
      return 0;
    }
    if (i + 1 == argc) {
      throw std::invalid_argument("Missing fixture option value");
    }
    std::string value = argv[++i];
    if (arg == "--artifact") {
      artifact_path = value;
    } else if (arg == "--storage") {
      storage_path = value;
    } else if (arg == "--product") {
      product = value;
    } else {
      throw std::invalid_argument("Unknown fixture option: " + arg);
    }
  }
  if (artifact_path.empty() || storage_path.empty() ||
      !ValidTelemetryProduct(product)) {
    throw std::invalid_argument("Artifact, storage and product are required");
  }
  auto size = std::filesystem::file_size(artifact_path);
  if (size > kTelemetryMaxOutboxBytes) {
    throw std::invalid_argument("Fixture artifact exceeds the outbox limit");
  }
  std::ifstream input(artifact_path, std::ios::binary);
  DataBuffer artifact(std::istreambuf_iterator<char>{input}, {});
  if (input.bad() || artifact.size() != size ||
      !ValidTelemetryArtifact(artifact)) {
    throw std::invalid_argument("Invalid fixture artifact");
  }
  std::filesystem::create_directories(storage_path);
  TelemetryOutbox outbox;
  outbox.pending.push_back({NewTelemetryBatchId(), product, artifact});
  auto expected = outbox.pending.front();
  auto state_path = storage_path / ("sender-outbox-" +
                                    TelemetryBatchIdText(expected.id) + ".bin");
  auto persist = [&] {
    DataBuffer bytes;
    auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
    if (!archive.Save(outbox)) {
      return false;
    }
    std::ofstream output(state_path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    return bool(output);
  };
  auto reload = [&] {
    std::ifstream file(state_path, std::ios::binary);
    DataBuffer bytes(std::istreambuf_iterator<char>{file}, {});
    auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
    TelemetryOutbox next;
    if (file.bad() || !archive.Load(next) ||
        archive.buffer().read_position() != bytes.size()) {
      throw std::runtime_error("Persisted fixture outbox did not reload");
    }
    outbox = std::move(next);
  };
  TelemetryKeeperStore store{storage_path / "received"};
  {
    TelemetryDeliveryPump pump{outbox, persist};
    TelemetryKeeperReceiver receiver{store};
    auto first = pump.Request(512);
    if (!first) {
      throw std::runtime_error("Fixture could not persist/send first frame");
    }
    (void)receiver.Receive(Source(), *first);
    pump.Written(true);
  }
  // Drop a partial receiver and all sender runtime state. The ordinary outbox
  // serialization alone supplies the same immutable id and artifact again.
  reload();
  if (outbox.pending.size() != 1 || outbox.pending.front().id != expected.id ||
      outbox.pending.front().artifact != expected.artifact) {
    throw std::runtime_error("Fixture restart changed its immutable batch");
  }
  {
    TelemetryDeliveryPump pump{outbox, persist};
    TelemetryKeeperReceiver receiver{store};
    std::optional<DataBuffer> ack;
    while (auto frame = pump.Request(512)) {
      if (auto response = receiver.Receive(Source(), *frame)) {
        ack = std::move(response);
      }
      pump.Written(true);
    }
    if (!ack) {
      throw std::runtime_error("Fixture keeper did not commit the artifact");
    }
    // Lose the application ACK and restart both ends once more.
  }
  auto target = store.PathFor(Source(), expected);
  auto committed_time = std::filesystem::last_write_time(target);
  reload();
  {
    TelemetryKeeperStore reopened{storage_path / "received"};
    TelemetryKeeperReceiver receiver{reopened};
    TelemetryDeliveryPump pump{outbox, persist};
    std::optional<DataBuffer> ack;
    while (auto frame = pump.Request(512)) {
      if (auto response = receiver.Receive(Source(), *frame)) {
        ack = std::move(response);
      }
      pump.Written(true);
    }
    if (!ack || !pump.Receive(*ack) || !outbox.pending.empty() ||
        std::filesystem::last_write_time(target) != committed_time) {
      throw std::runtime_error("Lost-ACK deduplication fixture failed");
    }
  }
  reload();
  if (!outbox.pending.empty()) {
    throw std::runtime_error("Durable ACK removal was not persisted");
  }
  std::cout << "SAVED_ARTIFACT=" << std::filesystem::absolute(target).string()
            << '\n';
  return 0;
}
}  // namespace ae::test_telemetry_delivery
void setUp() {}
void tearDown() {}
int test_telemetry_delivery() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_telemetry_delivery::test_Sha256MatchesPublishedVectors);
  RUN_TEST(ae::test_telemetry_delivery::
               test_OnlyDurableApplicationAckRemovesTheOutbox);
  RUN_TEST(ae::test_telemetry_delivery::
               test_ReceiverRestartAndLostAckDeduplicateWithoutRewriting);
  RUN_TEST(ae::test_telemetry_delivery::
               test_ConflictingFragmentsAndStorageFailureAreNotAcked);
  RUN_TEST(ae::test_telemetry_delivery::
               test_PendingLargeBatchSurvivesOrdinarySerialization);
  RUN_TEST(ae::test_telemetry_delivery::
               test_InvalidFrameAndWrongAckDoNotConsumeHistory);
  RUN_TEST(ae::test_telemetry_delivery::
               test_PersistFailureAndWriteRetryKeepTheSameImmutableBatch);
  RUN_TEST(
      ae::test_telemetry_delivery::
          test_OutboxRejectsUnboundedCountsAndAggregateBeforePayloadAllocation);
  RUN_TEST(
      ae::test_telemetry_delivery::
          test_OutboxRejectsDuplicateIdsInvalidProductsAndInvalidArtifacts);
  return UNITY_END();
}
int main(int argc, char** argv) {
  try {
    if (argc == 1) {
      return test_telemetry_delivery();
    }
    if (std::string_view{argv[1]} == "--real") {
      return ae::test_telemetry_delivery::RealArtifactFixture(argc, argv);
    }
    return ae::test_telemetry_delivery::ArtifactFixture(argc, argv);
  } catch (std::exception const& error) {
    std::cerr << "telemetry delivery fixture: " << error.what() << '\n';
    return 1;
  }
}
