/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unity.h>
#include <exception>
#include <memory>
#include <utility>
#include "aether/config.h"
#if AE_SUPPORT_MODEMS
#  include "aether/channels/modem_channel.h"
#  include "aether/clock.h"
#  include "aether/transport/modems/modem_transport.h"

namespace ae {
struct ModemChannelTestAccess {
  static TransportBuildSender ConnectTransport(
      std::unique_ptr<ByteIStream> stream) {
    return ModemChannel::ConnectTransport(std::move(stream));
  }
};
}  // namespace ae

namespace ae::test_modem_transport_shutdown {
class ConnectingStream final : public ByteIStream {
 public:
  void SetState(LinkState state) {
    info_.link_state = state;
    updates_.Emit();
  }
  WriteAction& Write(DataBuffer&&) override { return write_; }
  StreamInfo stream_info() const override { return info_; }
  StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{updates_};
  }
  OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{data_};
  }
  void Restream() override {}

 private:
  StreamInfo info_{};
  StreamUpdateEvent updates_;
  OutDataEvent data_;
  WriteAction write_;
};

struct ConnectResult {
  std::unique_ptr<ByteIStream> transport;
  int successes{};
  int errors{};
};
struct ConnectReceiver {
  using receiver_concept = ex::receiver_t;
  void set_value(std::unique_ptr<ByteIStream> transport) && noexcept {
    ++result->successes;
    result->transport = std::move(transport);
  }
  void set_error(int) && noexcept { ++result->errors; }
  void set_error(std::exception_ptr) && noexcept { ++result->errors; }
  void set_stopped() && noexcept { ++result->errors; }
  auto get_env() const noexcept { return ex::env{}; }
  ConnectResult* result;
};

void test_ConnectedTransportCanDisconnectWhileBuilderRemainsAlive() {
  auto stream = std::make_unique<ConnectingStream>();
  auto* raw = stream.get();
  raw->SetState(LinkState::kUnlinked);
  ConnectResult result;
  auto operation =
      ex::connect(ModemChannelTestAccess::ConnectTransport(std::move(stream)),
                  ConnectReceiver{&result});
  ex::start(operation);
  raw->SetState(LinkState::kLinked);
  TEST_ASSERT_EQUAL_INT(1, result.successes);
  TEST_ASSERT_NOT_NULL(result.transport.get());
  // Keep operation alive, just like the real channel builder after success.
  raw->SetState(LinkState::kLinkError);
  raw->SetState(LinkState::kLinked);
  TEST_ASSERT_EQUAL_INT(1, result.successes);
  TEST_ASSERT_EQUAL_INT(0, result.errors);
}

void test_ConnectionErrorCompletesOnlyOnce() {
  auto stream = std::make_unique<ConnectingStream>();
  auto* raw = stream.get();
  raw->SetState(LinkState::kUnlinked);
  ConnectResult result;
  auto operation =
      ex::connect(ModemChannelTestAccess::ConnectTransport(std::move(stream)),
                  ConnectReceiver{&result});
  ex::start(operation);
  raw->SetState(LinkState::kLinkError);
  raw->SetState(LinkState::kLinked);
  TEST_ASSERT_EQUAL_INT(1, result.errors);
  TEST_ASSERT_EQUAL_INT(0, result.successes);
}

void test_AlreadyFailedTransportCompletesImmediately() {
  auto stream = std::make_unique<ConnectingStream>();
  stream->SetState(LinkState::kLinkError);
  ConnectResult result;
  auto operation =
      ex::connect(ModemChannelTestAccess::ConnectTransport(std::move(stream)),
                  ConnectReceiver{&result});
  ex::start(operation);
  TEST_ASSERT_EQUAL_INT(1, result.errors);
  TEST_ASSERT_EQUAL_INT(0, result.successes);
}

struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->scheduler;
                   }};
    return AeCtx{const_cast<TestContext*>(this), &table};  // NOLINT
  }
  TaskScheduler scheduler;
};
class OpenedOperation final : public OpenNetworkOperation {
 public:
  OpenedOperation() { SetResult(Ok{ConnectionIndex{1}}); }
};
class StoppingDriver final : public IModemDriver {
 public:
  ModemOperation* Start() override { return nullptr; }
  ModemOperation* Stop() override { return nullptr; }
  OpenNetworkOperation* OpenNetwork(Protocol, std::string const&,
                                    std::uint16_t) override {
    return &opened_;
  }
  ModemOperation* CloseNetwork(ConnectionIndex) override {
    ++closed;
    return nullptr;
  }
  WriteOperation* WritePacket(ConnectionIndex,
                              std::span<std::uint8_t const>) override {
    ++rejected;
    return nullptr;
  }
  DataEvent::Subscriber data_event() override { return EventSubscriber{data_}; }
  ModemOperation* SetPowerSaveParam(ModemPowerSaveParam const&) override {
    return nullptr;
  }
  ModemOperation* PowerOff() override { return nullptr; }
  int closed{};
  int rejected{};

 private:
  OpenedOperation opened_;
  DataEvent data_;
};

void CheckFailureDestroysTransportAfterWriteFinishes(Protocol protocol) {
  TestContext context;
  StoppingDriver driver;
  Endpoint endpoint{};
  endpoint.protocol = protocol;
  endpoint.port = 9000;
  auto transport = std::make_unique<ModemTransport>(context, driver, endpoint);
  bool finished = false;
  bool finished_before_disconnect = false;
  int updates = 0;
  Subscription update_sub = transport->stream_update_event().Subscribe([&]() {
    ++updates;
    finished_before_disconnect = finished;
    // Unsubscribe before deletion to isolate the write action lifetime bug.
    update_sub.Reset();
    if (finished) {
      transport.reset();
    }
  });
  auto& write = transport->Write(DataBuffer{1, 2, 3});
  Subscription finished_sub =
      write.finished_event().Subscribe([&]() { finished = true; });
  for (int i = 0; i < 8; ++i) {
    context.scheduler.Update(Now());
  }
  // On the old implementation, retain the transport until Finish returns so
  // this regression reports the bad ordering instead of crashing the suite.
  auto destroyed_on_disconnect = transport == nullptr;
  transport.reset();
  TEST_ASSERT_TRUE(finished_before_disconnect);
  TEST_ASSERT_TRUE(destroyed_on_disconnect);
  TEST_ASSERT_NULL(transport.get());
  TEST_ASSERT_EQUAL_INT(1, driver.rejected);
  TEST_ASSERT_EQUAL_INT(1, driver.closed);
  TEST_ASSERT_EQUAL_INT(1, updates);
}
void test_UdpShutdownFailureFinishesBeforeTransportDestruction() {
  CheckFailureDestroysTransportAfterWriteFinishes(Protocol::kUdp);
}
void test_TcpShutdownFailureFinishesBeforeTransportDestruction() {
  CheckFailureDestroysTransportAfterWriteFinishes(Protocol::kTcp);
}
void test_DestructionCancelsPendingDisconnectWithoutNotifyingAgain() {
  TestContext context;
  StoppingDriver driver;
  Endpoint endpoint{};
  endpoint.protocol = Protocol::kUdp;
  auto transport = std::make_unique<ModemTransport>(context, driver, endpoint);
  int updates = 0;
  Subscription update_sub =
      transport->stream_update_event().Subscribe([&]() { ++updates; });
  auto& write = transport->Write(DataBuffer{1});
  bool finished = false;
  Subscription finished_sub =
      write.finished_event().Subscribe([&]() { finished = true; });
  for (int i = 0; i < 8 && !finished; ++i) {
    context.scheduler.Update(Now());
  }
  TEST_ASSERT_TRUE(finished);
  TEST_ASSERT_EQUAL_INT(0, updates);
  transport.reset();
  for (int i = 0; i < 8; ++i) {
    context.scheduler.Update(Now());
  }
  TEST_ASSERT_EQUAL_INT(0, updates);
  TEST_ASSERT_EQUAL_INT(1, driver.closed);
}
}  // namespace ae::test_modem_transport_shutdown
#endif
int test_modem_transport_shutdown() {
#if AE_SUPPORT_MODEMS
  using namespace ae::test_modem_transport_shutdown;  // NOLINT: Unity suite
                                                      // entry.
  UNITY_BEGIN();
  RUN_TEST(test_ConnectedTransportCanDisconnectWhileBuilderRemainsAlive);
  RUN_TEST(test_ConnectionErrorCompletesOnlyOnce);
  RUN_TEST(test_AlreadyFailedTransportCompletesImmediately);
  RUN_TEST(test_UdpShutdownFailureFinishesBeforeTransportDestruction);
  RUN_TEST(test_TcpShutdownFailureFinishesBeforeTransportDestruction);
  RUN_TEST(test_DestructionCancelsPendingDisconnectWithoutNotifyingAgain);
  return UNITY_END();
#else
  return 0;
#endif
}
