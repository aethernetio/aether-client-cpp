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

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <unity.h>

#include "aether/config.h"

#if AE_SUPPORT_MODEMS && AE_ENABLE_THINGY91X
#  include "aether/clock.h"
#  include "aether/modems/thingy91x_at_modem.h"
#  include "tests/test-serial-port/mock-serial-port.h"

namespace ae {
struct Thingy91xAtModemTestAccess {
  static void Connected(Thingy91xAtModem& modem) {
    modem.started_ = true;
    modem.connections_ = {4, 5};
    modem.SetupPoll();
  }
  static std::unique_ptr<Thingy91xAtModem> Create(
      AeContext const& context, std::unique_ptr<ISerialPort> serial,
      kModemMode mode) {
    auto init = ModemInit{};
    init.modem_mode = mode;
    return std::unique_ptr<Thingy91xAtModem>{
        new Thingy91xAtModem{context, std::move(init), std::move(serial)}};
  }
};

namespace test_thingy91x {
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

struct Fixture {
  explicit Fixture(kModemMode mode = kModemMode::kModeAuto) {
    auto port = std::make_unique<tests::MockSerialPort>();
    serial = port.get();
    writes = serial->write_event().Subscribe([this](auto data) {
      commands.emplace_back(reinterpret_cast<char const*>(data.data()),
                            data.size());
    });
    modem = Thingy91xAtModemTestAccess::Create(context, std::move(port), mode);
    Pump();
    Reply("OK\r\n");
    Reply("OK\r\n");
    commands.clear();
  }

  void Pump() {
    for (int i = 0; i < 8; ++i) {
      context.scheduler.Update(now);
    }
  }
  void Reply(std::string_view response) {
    serial->WriteOut(std::span<std::uint8_t const>{
        reinterpret_cast<std::uint8_t const*>(response.data()),
        response.size()});
    Pump();
  }
  void Open() {
    auto* operation = modem->OpenNetwork(Protocol::kTcp, "127.0.0.1", 9000);
    TEST_ASSERT_NOT_NULL(operation);
    result_sub = operation->result_event().Subscribe(
        [this](auto res) { result.emplace(std::move(res)); });
    Pump();
    Expect("AT#XSOCKET=1,1,0\r\n");
  }
  void Connect() {
    Open();
    Reply("#XSOCKET: 4,1,6\r\nOK\r\n");
    Expect("AT#XSOCKETSELECT=4\r\n");
    Reply("OK\r\n");
    Expect("AT#XSOCKETOPT=1,20,30\r\n");
    Reply("OK\r\n");
    Expect("AT#XCONNECT=\"127.0.0.1\",9000\r\n");
  }
  void Expect(char const* command) {
    TEST_ASSERT_FALSE(commands.empty());
    TEST_ASSERT_EQUAL_STRING(command, commands.back().c_str());
  }
  void CompleteRollback(int error) {
    Expect("AT#XSOCKETSELECT=4\r\n");
    TEST_ASSERT_FALSE(result.has_value());
    Reply("OK\r\n");
    Expect("AT#XSOCKET=0\r\n");
    TEST_ASSERT_FALSE(result.has_value());
    Reply("#XSOCKET: 4,0\r\nOK\r\n");
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_FALSE(result->IsOk());
    TEST_ASSERT_EQUAL_INT(error, static_cast<int>(result->error()));
  }

  TestContext context;
  TimePoint now{Now()};
  tests::MockSerialPort* serial{};
  std::vector<std::string> commands;
  std::optional<OpenNetworkOperation::ResultType> result;
  Subscription writes;
  Subscription result_sub;
  std::unique_ptr<Thingy91xAtModem> modem;
};

void CheckStartMode(kModemMode mode, char const* command) {
  Fixture f{mode};
  auto* start = f.modem->Start();
  TEST_ASSERT_NOT_NULL(start);
  f.Pump();
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("OK\r\n");
  f.Expect(command);
  // A rejected mode must finish with an error before dependent AT commands.
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(start->is_finished());
  TEST_ASSERT_TRUE(start->result().has_value());
  TEST_ASSERT_TRUE(start->result()->IsErr());
  f.Expect(command);
}

void test_StartSelectsNbIot() {
  CheckStartMode(kModemMode::kModeNbIot, "AT%XSYSTEMMODE=0,1,0,2\r\n");
}

void test_StartSelectsLteM() {
  CheckStartMode(kModemMode::kModeCatM, "AT%XSYSTEMMODE=1,0,0,1\r\n");
}

void test_CreateFailureDoesNotCloseAnotherSocket() {
  Fixture f;
  f.Open();
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(f.result.has_value());
  TEST_ASSERT_FALSE(f.result->IsOk());
  TEST_ASSERT_EQUAL_UINT(1, f.commands.size());
}

void test_ConnectFailureClosesSocketBeforeReportingError() {
  Fixture f;
  f.Connect();
  f.Reply("ERROR\r\n");
  f.CompleteRollback(-1);
}

void test_ConnectTimeoutClosesSocketBeforeReportingError() {
  Fixture f;
  f.Connect();
  f.context.scheduler.Update(f.now + std::chrono::seconds{11});
  f.now = Now();
  f.Pump();
  f.CompleteRollback(-2);
}

void test_RollbackSelectFailureDoesNotCloseAnotherSocket() {
  Fixture f;
  f.Connect();
  f.Reply("ERROR\r\n");
  f.Expect("AT#XSOCKETSELECT=4\r\n");
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(f.result.has_value());
  TEST_ASSERT_FALSE(f.result->IsOk());
  TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(f.result->error()));
  f.Expect("AT#XSOCKETSELECT=4\r\n");
}

void test_StopClosesSocketsBeforeDeactivation() {
  Fixture f;
  Thingy91xAtModemTestAccess::Connected(*f.modem);
  auto* stop = f.modem->Stop();
  bool closed_at_result = false;
  Subscription stopped = stop->result_event().Subscribe(
      [&](auto const&) { closed_at_result = !f.serial->IsOpen(); });
  TEST_ASSERT_EQUAL_PTR(stop, f.modem->Stop());
  TEST_ASSERT_NULL(f.modem->OpenNetwork(Protocol::kTcp, "127.0.0.1", 9000));
  TEST_ASSERT_NULL(f.modem->WritePacket(4, {}));
  TEST_ASSERT_NULL(f.modem->Start());
  f.Pump();
  f.Expect("AT#XSOCKETSELECT=4\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT#XSOCKET=0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT#XSOCKETSELECT=5\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT#XSOCKET=0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CFUN=0\r\n");
  TEST_ASSERT_FALSE(stop->is_finished());
  TEST_ASSERT_TRUE(f.serial->IsOpen());
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_TRUE(closed_at_result);
  TEST_ASSERT_TRUE(stop->result()->IsOk());
  TEST_ASSERT_EQUAL_PTR(stop, f.modem->Stop());
  TEST_ASSERT_EQUAL_PTR(stop, f.modem->CloseNetwork(4));
  f.context.scheduler.Update(f.now + std::chrono::seconds{1});
  f.Pump();
  TEST_ASSERT_EQUAL_UINT(5, f.commands.size());
}

void test_StopContinuesAfterSelectErrorAndCloseTimeout() {
  Fixture f;
  Thingy91xAtModemTestAccess::Connected(*f.modem);
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Expect("AT#XSOCKETSELECT=4\r\n");
  f.Reply("ERROR\r\n");
  f.Expect("AT#XSOCKETSELECT=5\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT#XSOCKET=0\r\n");
  f.context.scheduler.Update(f.now + std::chrono::seconds{11});
  f.now = Now();
  f.Pump();
  f.Expect("AT+CFUN=0\r\n");
  TEST_ASSERT_FALSE(stop->is_finished());
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_FALSE(stop->result()->IsOk());
  TEST_ASSERT_EQUAL_UINT(4, f.commands.size());
}

void test_StopDeactivationTimeoutFinishes() {
  Fixture f;
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Expect("AT+CFUN=0\r\n");
  f.context.scheduler.Update(f.now + std::chrono::seconds{31});
  f.now = Now();
  f.Pump();
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_FALSE(stop->result()->IsOk());
}

void test_StopIncludesSocketOpenedByPendingOperation() {
  Fixture f;
  f.Connect();
  auto* stop = f.modem->Stop();
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(f.result.has_value());
  TEST_ASSERT_TRUE(f.result->IsOk());
  f.Expect("AT#XSOCKETSELECT=4\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT#XSOCKET=0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_TRUE(stop->result()->IsOk());
}
}  // namespace test_thingy91x
}  // namespace ae
#endif

int test_thingy91x() {
#if AE_SUPPORT_MODEMS && AE_ENABLE_THINGY91X
  using namespace ae::test_thingy91x;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(test_StartSelectsNbIot);
  RUN_TEST(test_StartSelectsLteM);
  RUN_TEST(test_CreateFailureDoesNotCloseAnotherSocket);
  RUN_TEST(test_ConnectFailureClosesSocketBeforeReportingError);
  RUN_TEST(test_ConnectTimeoutClosesSocketBeforeReportingError);
  RUN_TEST(test_RollbackSelectFailureDoesNotCloseAnotherSocket);
  RUN_TEST(test_StopClosesSocketsBeforeDeactivation);
  RUN_TEST(test_StopContinuesAfterSelectErrorAndCloseTimeout);
  RUN_TEST(test_StopDeactivationTimeoutFinishes);
  RUN_TEST(test_StopIncludesSocketOpenedByPendingOperation);
  return UNITY_END();
#else
  return 0;
#endif
}
