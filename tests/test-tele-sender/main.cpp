/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <unity.h>
#include <limits>
#include <memory>
#include <vector>
#include "aether-tele/raw.h"
#include "aether/ae_actions/telemetry_payload.h"
#include "aether/ae_actions/telemetry_send_queue.h"
#include "aether/aether_tele.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/work_cloud_api/work_server_api/login_api.h"

namespace {
struct Context {
  ae::TaskScheduler& scheduler() const { return tasks; }
  ae::EventSystem& event_system() const { return events; }
  mutable ae::TaskScheduler tasks;
  mutable ae::EventSystem events;
};
class PendingWrite final : public ae::WriteAction {
 public:
  explicit PendingWrite(Context& context) : WriteAction{context} {}
  void Complete(Status status) { SetStatus(status); }
};
struct Transport {
  Transport()
      : queue{context.tasks,
              [this](ae::Telemetric&& payload) -> ae::WriteAction* {
                if (reject) return nullptr;
                TEST_ASSERT_NULL(active.get());
                received.emplace_back(std::move(payload));
                active = std::make_unique<PendingWrite>(context);
                return active.get();
              },
              [this](bool success) {
                ++completed;
                last_success = success;
              }} {}
  void Complete(ae::WriteAction::Status status) {
    TEST_ASSERT_NOT_NULL(active.get());
    active->Complete(status);
    // Model a real action pool: cleanup is queued by Finish, after its status
    // callback queued the upload continuation. Next send must wait for this.
    context.tasks.Task([this]() { active.reset(); });
  }
  void Tick() { context.tasks.Update(); }
  void FinishAll() {
    std::size_t turns{};
    while (queue.busy()) {
      TEST_ASSERT_TRUE(++turns < 100000);
      if (active && !active->is_finished())
        Complete(ae::WriteAction::Status::kSuccess);
      Tick();
    }
    TEST_ASSERT_NULL(active.get());
  }
  Context context;
  std::unique_ptr<PendingWrite> active;
  std::vector<ae::Telemetric> received;
  unsigned completed{};
  bool last_success{};
  bool reject{};
  ae::telemetry_internal::FragmentSendQueue queue;
};
ae::TeleStatisticsArchive Archive() {
  struct Identity {};
  using Space = ae::tele::raw::Space<Identity>;
  Space space;
  ae::TeleStatisticsArchive archive;
  auto& bytes = archive.spaces["aether"].blocks;
  auto append = [](void* context, std::span<std::uint8_t const> block) {
    auto& target = *static_cast<std::vector<std::uint8_t>*>(context);
    target.insert(target.end(), block.begin(), block.end());
    return true;
  };
  ae::tele::raw::Stream<Space, 8192> stream(space, 3, append, &bytes);
  ae::tele::raw::Tag<Space> tag{7, 1, 1, "large", "large", __FILE__, __LINE__};
  std::vector<std::uint8_t> blob(6000, 42);
  TEST_ASSERT_TRUE(stream.Emit(tag, ae::tele::raw::Kind::kLog, space.Runtime(),
                               {blob}, 5, 100));
  TEST_ASSERT_TRUE(stream.Drain());
  archive.spaces["aether"].catalog = "test-catalog";
  archive.spaces["aether"].environment.compiler = std::string(100000, 'x');
  return archive;
}
class SizedCipher final : public ae::IEncryptProvider {
 public:
  explicit SizedCipher(std::size_t overhead) : overhead_{overhead} {}
  ae::DataBuffer Encrypt(ae::DataBuffer const& bytes) override {
    ++calls;
    auto result = bytes;
    result.resize(bytes.size() + overhead_);
    return result;
  }
  std::size_t EncryptOverhead() const override { return overhead_; }
  unsigned calls{};

 private:
  std::size_t overhead_;
};
std::size_t InnerSize(ae::Telemetric const& payload) {
  ae::AuthorizedApi api;
  ae::ApiContext context{api};
  context->SendTelemetry(payload);
  return std::move(context).Pack().size();
}
std::size_t OuterSize(ae::Telemetric const& payload, SizedCipher& cipher,
                      ae::ProtocolContext& protocol) {
  ae::LoginApi api{cipher};
  ae::ApiContext context{api, protocol};
  context->LoginByAlias(
      ae::Uid{}, ae::SubApi<ae::AuthorizedApi>{[&payload](auto& authorized) {
        authorized->SendTelemetry(payload);
      }});
  return std::move(context).Pack().size();
}
void test_FragmentsFitActualLoginEncryptionPacketsWithoutSearchSideEffects() {
  Context services;
  ae::ProtocolContext protocol{services};
  auto archive = Archive();
  TEST_ASSERT_TRUE(
      ae::MakeTelemetryPayloads(
          archive, std::numeric_limits<std::size_t>::max(),
          [](std::size_t) { return std::numeric_limits<std::size_t>::max(); })
          .empty());
  auto& sink = ae::AetherTeleSink::Instance();
  auto runtime = sink.Runtime();
  sink.SetRuntime({false, false, false, false, 0});
  ae::Telemetric minimum{};
  auto writer =
      ae::seri::BinaryArchive{ae::seri::BinaryVectorBuffer<>{minimum.cpp.blob}};
  TEST_ASSERT_TRUE(writer.Save(ae::TelemetryFragment{{}, 0, 1, {0}}).IsOk());
  for (std::size_t overhead : {0u, 24u, 44u}) {
    for (std::size_t mtu : {96u, 512u, 1200u}) {
      SizedCipher cipher{overhead};
      auto payloads = ae::MakeTelemetryPayloads(
          archive, mtu, [overhead](std::size_t inner) {
            return ae::client_server_connection_internal::AuthorizedApiCallSize(
                inner, overhead);
          });
      // Candidate search never encrypts and therefore never consumes a nonce.
      TEST_ASSERT_EQUAL_UINT(0, cipher.calls);
      if (payloads.empty()) {
        TEST_ASSERT_TRUE(OuterSize(minimum, cipher, protocol) > mtu);
        continue;
      }
      for (auto const& payload : payloads) {
        auto size = OuterSize(payload, cipher, protocol);
        TEST_ASSERT_TRUE(size <= mtu);
        TEST_ASSERT_EQUAL_UINT(
            size, ae::client_server_connection_internal::AuthorizedApiCallSize(
                      InnerSize(payload), overhead));
      }
      auto restored = ae::ReassembleTelemetryPayloads(payloads);
      TEST_ASSERT_TRUE(restored.has_value());
      TEST_ASSERT_TRUE(restored->spaces["aether"].blocks ==
                       archive.spaces["aether"].blocks);
    }
  }
  sink.SetRuntime(runtime);
}
void test_LargeSnapshotUsesOneLiveActionBeyondEventPoolCapacity() {
  auto archive = Archive();
  auto payloads = ae::MakeTelemetryPayloads(archive, 96);
  TEST_ASSERT_TRUE(payloads.size() > 4 * ae::kEventsCapacity);
  auto expected = payloads.size();
  Transport transport;
  // Leave only a small constant event-key headroom. Thousands of simultaneous
  // WriteAction instances would abort; the complete transfer still succeeds.
  std::vector<ae::Event<void()>> occupied;
  occupied.reserve(ae::kEventsCapacity - 8);
  for (std::size_t i = 0; i < ae::kEventsCapacity - 8; ++i)
    occupied.emplace_back(transport.context);
  TEST_ASSERT_TRUE(transport.queue.Start(std::move(payloads)));
  TEST_ASSERT_EQUAL_UINT(1, transport.received.size());
  TEST_ASSERT_FALSE(
      transport.queue.Start(ae::MakeTelemetryPayloads(archive, 512)));
  transport.FinishAll();
  TEST_ASSERT_EQUAL_UINT(expected, transport.received.size());
  TEST_ASSERT_EQUAL_UINT(1, transport.completed);
  TEST_ASSERT_TRUE(transport.last_success);
  TEST_ASSERT_EQUAL_UINT(0, transport.context.tasks.overflow_counter());
  auto restored = ae::ReassembleTelemetryPayloads(transport.received);
  TEST_ASSERT_TRUE(restored.has_value());
  TEST_ASSERT_TRUE(restored->spaces["aether"].blocks ==
                   archive.spaces["aether"].blocks);
  TEST_ASSERT_TRUE(restored->spaces["aether"].environment.compiler ==
                   archive.spaces["aether"].environment.compiler);
}
void test_FailureOrStopAbortsAndLaterSnapshotCanRetry() {
  auto archive = Archive();
  for (auto status :
       {ae::WriteAction::Status::kFail, ae::WriteAction::Status::kStop}) {
    Transport transport;
    TEST_ASSERT_TRUE(
        transport.queue.Start(ae::MakeTelemetryPayloads(archive, 1200)));
    transport.Complete(status);
    transport.Tick();
    transport.Tick();
    TEST_ASSERT_FALSE(transport.queue.busy());
    TEST_ASSERT_EQUAL_UINT(1, transport.received.size());
    TEST_ASSERT_EQUAL_UINT(1, transport.completed);
    TEST_ASSERT_FALSE(transport.last_success);
    transport.received.clear();
    auto retry = ae::MakeTelemetryPayloads(archive, 1200);
    auto expected = retry.size();
    TEST_ASSERT_TRUE(transport.queue.Start(std::move(retry)));
    transport.FinishAll();
    TEST_ASSERT_EQUAL_UINT(2, transport.completed);
    TEST_ASSERT_TRUE(transport.last_success);
    TEST_ASSERT_EQUAL_UINT(expected, transport.received.size());
    TEST_ASSERT_TRUE(
        ae::ReassembleTelemetryPayloads(transport.received).has_value());
  }
}
void test_CancelledContinuationAndRejectedEnqueueDoNotSendMore() {
  auto archive = Archive();
  Transport transport;
  TEST_ASSERT_TRUE(
      transport.queue.Start(ae::MakeTelemetryPayloads(archive, 1200)));
  transport.Complete(ae::WriteAction::Status::kSuccess);
  transport.queue.Cancel();
  transport.Tick();
  transport.Tick();
  TEST_ASSERT_FALSE(transport.queue.busy());
  TEST_ASSERT_EQUAL_UINT(1, transport.received.size());
  TEST_ASSERT_EQUAL_UINT(0, transport.completed);
  transport.reject = true;
  TEST_ASSERT_TRUE(
      transport.queue.Start(ae::MakeTelemetryPayloads(archive, 1200)));
  TEST_ASSERT_FALSE(transport.queue.busy());
  TEST_ASSERT_EQUAL_UINT(1, transport.completed);
  TEST_ASSERT_FALSE(transport.last_success);
  TEST_ASSERT_EQUAL_UINT(1, transport.received.size());
}
}  // namespace
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(
      test_FragmentsFitActualLoginEncryptionPacketsWithoutSearchSideEffects);
  RUN_TEST(test_LargeSnapshotUsesOneLiveActionBeyondEventPoolCapacity);
  RUN_TEST(test_FailureOrStopAbortsAndLaterSnapshotCanRetry);
  RUN_TEST(test_CancelledContinuationAndRejectedEnqueueDoNotSendMore);
  return UNITY_END();
}
