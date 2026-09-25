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
#include <string>
#include <string_view>
#include <vector>

#include <unity.h>
#include "aether/config.h"
#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070
#  include "aether/clock.h"
#  include "aether/modems/sim7070_at_modem.h"
#  include "tests/test-serial-port/mock-serial-port.h"

namespace ae {
struct Sim7070AtModemTestAccess {
  static std::unique_ptr<Sim7070AtModem> Create(
      AeContext const& context, std::unique_ptr<ISerialPort> serial) {
    return std::unique_ptr<Sim7070AtModem>{
        new Sim7070AtModem{context, ModemInit{}, std::move(serial)}};
  }
  static void Connected(Sim7070AtModem& modem) {
    modem.started_ = true;
    modem.connections_ = {0, 1};
    modem.SetupPoll();
  }
};
namespace test_sim7070 {
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
  Fixture() {
    auto port = std::make_unique<tests::MockSerialPort>();
    serial = port.get();
    writes = serial->write_event().Subscribe([this](auto data) {
      commands.emplace_back(reinterpret_cast<char const*>(data.data()),
                            data.size());
    });
    modem = Sim7070AtModemTestAccess::Create(context, std::move(port));
    Pump();
    Reply("OK\r\n");
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
  void Expect(char const* command) {
    TEST_ASSERT_FALSE(commands.empty());
    TEST_ASSERT_EQUAL_STRING(command, commands.back().c_str());
  }
  TestContext context;
  TimePoint now{Now()};
  tests::MockSerialPort* serial{};
  std::vector<std::string> commands;
  Subscription writes;
  std::unique_ptr<Sim7070AtModem> modem;
};

void test_RemoteCloseNotifiesOnlyTrackedConnectionOnce() {
  Fixture f;
  Sim7070AtModemTestAccess::Connected(*f.modem);
  std::vector<ConnectionIndex> closed;
  Subscription subscription = f.modem->connection_closed_event().Subscribe(
      [&](ConnectionIndex connection) { closed.push_back(connection); });
  f.Reply(
      "+CASTATE: 0,1\r\n+CASTATE: 8,0\r\n+CASTATE: 256,0\r\n"
      "+CASTATE: invalid\r\n");
  TEST_ASSERT_TRUE(closed.empty());
  f.Reply("+CASTATE: 0,0\r\n+CASTATE: 0,0\r\n+CASTATE: 1,0\r\n");
  TEST_ASSERT_EQUAL_UINT(2, closed.size());
  TEST_ASSERT_EQUAL_INT(0, closed[0]);
  TEST_ASSERT_EQUAL_INT(1, closed[1]);
  TEST_ASSERT_TRUE(f.commands.empty());
}

void test_StopClosesSocketsAndWaitsForContextDeactivation() {
  Fixture f;
  Sim7070AtModemTestAccess::Connected(*f.modem);
  auto* stop = f.modem->Stop();
  bool closed_at_result = false;
  Subscription stopped = stop->result_event().Subscribe(
      [&](auto const&) { closed_at_result = !f.serial->IsOpen(); });
  TEST_ASSERT_EQUAL_PTR(stop, f.modem->Stop());
  TEST_ASSERT_NULL(f.modem->OpenNetwork(Protocol::kTcp, "127.0.0.1", 9000));
  TEST_ASSERT_NULL(f.modem->WritePacket(0, {}));
  TEST_ASSERT_NULL(f.modem->Start());
  f.Pump();
  f.Expect("AT+CACLOSE=0\r\n");
  f.Reply("+CADATAIND: 0\r\nOK\r\n");
  f.Expect("AT+CACLOSE=1\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CNACT?\r\n");
  f.Reply("+CNACT: 0,1,\"10.0.0.1\"\r\nOK\r\n");
  f.Expect("AT+CNACT=0,0\r\n");
  f.Reply("+APP PDP: 0,DEACTIVE\r\n");
  TEST_ASSERT_FALSE(stop->is_finished());
  f.Expect("AT+CNACT=0,0\r\n");
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
  TEST_ASSERT_EQUAL_PTR(stop, f.modem->CloseNetwork(0));
  f.Pump();
  TEST_ASSERT_EQUAL_UINT(5, f.commands.size());
}

void test_StopContinuesAfterCloseErrorAndDeactivationTimeout() {
  Fixture f;
  Sim7070AtModemTestAccess::Connected(*f.modem);
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Expect("AT+CACLOSE=0\r\n");
  f.Reply("ERROR\r\n");
  f.Expect("AT+CACLOSE=1\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CNACT?\r\n");
  f.Reply("+CNACT: 0,1,\"10.0.0.1\"\r\nOK\r\n");
  f.Expect("AT+CNACT=0,0\r\n");
  f.Reply("OK\r\n");
  f.context.scheduler.Update(f.now + std::chrono::seconds{11});
  f.now = Now();
  f.Pump();
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_FALSE(stop->result()->IsOk());
}

void test_StopSkipsAlreadyInactiveContext() {
  Fixture f;
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Expect("AT+CNACT?\r\n");
  f.Reply("+CNACT: 0,0,\"0.0.0.0\"\r\nOK\r\n");
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_TRUE(stop->result()->IsOk());
  TEST_ASSERT_EQUAL_UINT(2, f.commands.size());
}

void test_StopDeactivationErrorFinishes() {
  Fixture f;
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Reply("+CNACT: 0,0,\"0.0.0.0\"\r\nOK\r\n");
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_FALSE(stop->result()->IsOk());
}

void test_StartActivatesContextBeforeShutdown() {
  Fixture f;
  auto* start = f.modem->Start();
  TEST_ASSERT_NOT_NULL(start);
  f.Pump();
  f.Expect("AT+CFUN=1,0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CPIN?\r\n");
  f.Reply("+CPIN: READY\r\nOK\r\n");
  f.Expect("AT+IPR=115200\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CNMP=2\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CREG=1;+CGREG=1;+CEREG=1\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CEREG?\r\n");
  f.Reply("+CEREG: 1,0\r\nOK\r\n");
  f.Expect("AT+COPS=0\r\n");
  f.Reply("+CME ERROR: 13\r\n");
  TEST_ASSERT_FALSE(start->is_finished());
  f.context.scheduler.Update(Now() + std::chrono::milliseconds{1100});
  f.now = Now();
  f.Pump();
  f.Expect("AT+COPS=0\r\n");
  TEST_ASSERT_FALSE(start->is_finished());
  f.Reply("OK\r\n");
  f.Expect("AT+CEREG?\r\n");
  f.Reply("+CEREG: 1,1\r\nOK\r\n");
  f.Expect("AT+CGDCONT=1,\"IP\",\"\"\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CNCFG=0,0,\"\",\"\",\"\",0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CNACT?\r\n");
  f.Reply("+CNACT: 0,0,\"0.0.0.0\"\r\nOK\r\n");
  f.Expect("AT+CNACT=0,1\r\n");
  f.Reply("OK\r\n+APP PDP: 0,ACTIVE\r\n");
  f.Expect("AT+CASTATE?\r\n");
  f.Reply("+CASTATE: 0,1\r\nOK\r\n");
  f.Expect("AT+CACLOSE=0\r\n");
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(start->is_finished());
  TEST_ASSERT_TRUE(start->result()->IsOk());
  auto* stop = f.modem->Stop();
  f.Pump();
  f.Expect("AT+CNACT?\r\n");
  f.Reply("+CNACT: 0,1,\"10.0.0.1\"\r\nOK\r\n");
  f.Expect("AT+CNACT=0,0\r\n");
  f.Reply("OK\r\n+APP PDP: 0,DEACTIVE\r\n");
  f.Expect("AT+CFUN=0\r\n");
  f.Reply("OK\r\n");
  TEST_ASSERT_TRUE(stop->is_finished());
  TEST_ASSERT_FALSE(f.serial->IsOpen());
  TEST_ASSERT_TRUE(stop->result()->IsOk());
}

void test_StartDoesNotQuerySimWhenEnablingModemFails() {
  Fixture f;
  auto* start = f.modem->Start();
  f.Pump();
  f.Expect("AT+CFUN=1,0\r\n");
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(start->is_finished());
  TEST_ASSERT_FALSE(start->result()->IsOk());
  TEST_ASSERT_EQUAL_UINT(1, f.commands.size());
}

void test_StartRetriesSimWhileInterfaceIsWakingUp() {
  Fixture f;
  auto* start = f.modem->Start();
  f.Pump();
  f.Expect("AT+CFUN=1,0\r\n");
  f.Reply("OK\r\n");
  f.Expect("AT+CPIN?\r\n");
  f.Reply("+CME ERROR: 13\r\n");
  TEST_ASSERT_FALSE(start->is_finished());
  TEST_ASSERT_EQUAL_UINT(2, f.commands.size());
  f.context.scheduler.Update(Now() + std::chrono::milliseconds{600});
  f.now = Now();
  f.Pump();
  TEST_ASSERT_EQUAL_UINT(3, f.commands.size());
  f.Expect("AT+CPIN?\r\n");
  f.Reply("+CPIN: READY\r\nOK\r\n");
  f.Expect("AT+IPR=115200\r\n");
  f.Reply("ERROR\r\n");
  TEST_ASSERT_TRUE(start->is_finished());
}

void test_StartSimReadinessTimeoutFinishes() {
  Fixture f;
  auto* start = f.modem->Start();
  f.Pump();
  f.Reply("OK\r\n");
  f.Expect("AT+CPIN?\r\n");
  f.context.scheduler.Update(Now() + std::chrono::seconds{16});
  f.now = Now();
  f.Pump();
  TEST_ASSERT_TRUE(start->is_finished());
  TEST_ASSERT_FALSE(start->result()->IsOk());
  TEST_ASSERT_EQUAL_UINT(2, f.commands.size());
}
}  // namespace test_sim7070
}  // namespace ae
#endif

int test_sim7070() {
#if AE_SUPPORT_MODEMS && AE_ENABLE_SIM7070
  using namespace ae::test_sim7070;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(test_RemoteCloseNotifiesOnlyTrackedConnectionOnce);
  RUN_TEST(test_StopClosesSocketsAndWaitsForContextDeactivation);
  RUN_TEST(test_StopContinuesAfterCloseErrorAndDeactivationTimeout);
  RUN_TEST(test_StopSkipsAlreadyInactiveContext);
  RUN_TEST(test_StopDeactivationErrorFinishes);
  RUN_TEST(test_StartActivatesContextBeforeShutdown);
  RUN_TEST(test_StartDoesNotQuerySimWhenEnablingModemFails);
  RUN_TEST(test_StartRetriesSimWhileInterfaceIsWakingUp);
  RUN_TEST(test_StartSimReadinessTimeoutFinishes);
  return UNITY_END();
#else
  return 0;
#endif
}
