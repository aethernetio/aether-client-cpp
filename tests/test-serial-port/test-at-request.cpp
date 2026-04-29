/*
 * Copyright 2025 Aethernet Inc.
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

#include <string>
#include <iterator>
#include <algorithm>

#include "aether/executors/executors.h"
#include "aether/tasks/manual_task_scheduler.h"

#include "aether/serial_ports/at_support/at_support.h"
#include "aether/serial_ports/at_support/at_request.h"

#include "tests/test-serial-port/mock-serial-port.h"

namespace ae::test_at_request {

using TaskScheduler = ManualTaskScheduler<TaskManagerConf<10>>;
struct TestContext {
  auto& scheduler() const noexcept { return *sched; }
  TaskScheduler* sched;
};

void test_AtRequestStringCommand0WaitsSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  DataBuffer received;
  mock_serial.write_event().Subscribe([&](auto data) {
    std::copy(std::begin(data), std::end(data), std::back_inserter(received));
  });

  // Create AtRequest with string command and 0 waits
  auto at_request = at::MakeRequest(ex::just(), at_support, std::string{"AT"});

  auto res = ex::SyncWait(std::move(at_request));

  TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", received.data(), 5);
}

void test_AtRequestStringCommand0WaitsError() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  DataBuffer received;
  mock_serial.write_event().Subscribe([&](auto data) {
    std::copy(std::begin(data), std::end(data), std::back_inserter(received));
  });

  // Create AtRequest with string command and 0 waits
  auto at_request = at::MakeRequest(ex::just(), at_support, std::string{"AT"});

  // Close serial port to simulate error condition
  mock_serial.close();

  auto res = ex::SyncWait(std::move(at_request));

  TEST_ASSERT_TRUE(res.has_value());
  TEST_ASSERT_TRUE(res->IsErr());
  TEST_ASSERT_EQUAL(-1, res->error());
}

void test_AtRequestStringCommand1WaitSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 1 wait for "OK"
  auto at_request = at::MakeRequest(ex::just(), at_support, std::string{"AT"},
                                    at::Wait{"OK"});

  mock_serial.write_event().Subscribe([&](auto data) {
    TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", data.data(), 5);
    // Add expected response "OK" to trigger wait observer
    std::string_view ok_line{"OK\r\n"};
    mock_serial.WriteOut(std::span<std::uint8_t const>{
        reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});
  });

  auto res = ex::SyncWait(std::move(at_request));

  TEST_ASSERT_TRUE(res.has_value());
  TEST_ASSERT_TRUE(res->IsOk());
}

struct AtReceiveState {
  bool res{false};
  bool error{false};
  bool timeout{false};
};

struct AtReceiver {
  using receiver_concept = ex::receiver_t;

  void set_value(auto&&...) && noexcept { state->res = true; }
  void set_error(ex::TimeoutError&&) && noexcept { state->timeout = true; }
  void set_error(auto&&...) && noexcept { state->error = true; }
  void set_stopped(auto&&...) && noexcept {}

  AtReceiveState* state;
};

void test_AtRequestStringCommand1WaitErrorTimeout() {
  TaskScheduler scheduler;

  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  DataBuffer received;
  mock_serial.write_event().Subscribe([&](auto data) {
    TEST_ASSERT_EQUAL_STRING_LEN("AT\r\n", data.data(), 5);
    std::copy(std::begin(data), std::end(data), std::back_inserter(received));
  });

  // Create AtRequest with string command and 1 wait with short timeout
  auto at_request =
      at::MakeRequest(ex::just(), at_support, "AT", at::Wait{"OK"}) |
      ex::with_timeout(TestContext{&scheduler}, 100ms);

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  // no result
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);

  scheduler.Update();
  scheduler.Update(Now() + 100ms);

  // expected error
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_TRUE(s.timeout);
}

void test_AtRequestStringCommand1WaitErrorResponse() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 1 wait
  auto at_request =
      ex::just() | at::MakeRequest(at_support, "AT", at::Wait{"OK"});

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  std::string_view error_line{"ERROR\r\n"};
  mock_serial.WriteOut(
      std::span{reinterpret_cast<std::uint8_t const*>(error_line.data()),
                error_line.size()});

  // expected error
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_TRUE(s.error);
  TEST_ASSERT_FALSE(s.timeout);
}

void test_AtRequestStringCommand2WaitsSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 2 waits
  auto at_request =
      ex::just() |
      at::MakeRequest(at_support, "AT", at::Wait{"OK"}, at::Wait{"READY"});

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  // Add first expected response "OK"
  std::string_view ok_line{"OK\r\n"};
  mock_serial.WriteOut(std::span{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // no res yet
  TEST_ASSERT_FALSE(s.res);

  std::string_view ready_line{"READY\r\n"};
  mock_serial.WriteOut(
      std::span{reinterpret_cast<std::uint8_t const*>(ready_line.data()),
                ready_line.size()});

  // got result
  TEST_ASSERT_TRUE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);
}

void test_AtRequestStringCommand2WaitsErrorPartial() {
  TaskScheduler scheduler;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 2 waits with short timeout
  auto at_request = at::MakeRequest(ex::just(), at_support, "AT",
                                    at::Wait{"OK"}, at::Wait{"READY"}) |
                    ex::with_timeout(TestContext{&scheduler}, 200ms);

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);
  scheduler.Update(Now());

  // Add only first expected response "OK"
  ae::DataBuffer data;
  std::string_view ok_line{"OK\r\n"};
  mock_serial.WriteOut(std::span{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // no res yet
  TEST_ASSERT_FALSE(s.res);

  scheduler.Update(Now() + 200ms);
  // expected timeout
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_TRUE(s.timeout);
}

void test_AtRequestStringCommand5WaitsSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 5 waits
  auto at_request =
      ex::just() | at::MakeRequest(at_support, "AT", at::Wait{"RESP1"},
                                   at::Wait{"RESP2"}, at::Wait{"RESP3"},
                                   at::Wait{"RESP4"}, at::Wait{"RESP5"});

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  // Add all expected responses

  std::string_view responses[] =  // NOLINT
      {"RESP1\r\n", "RESP2\r\n", "RESP3\r\n", "RESP4\r\n", "RESP5\r\n"};
  for (auto const& resp : responses) {
    mock_serial.WriteOut(std::span{
        reinterpret_cast<std::uint8_t const*>(resp.data()), resp.size()});
  }
  // got result
  TEST_ASSERT_TRUE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);
}

void test_AtRequestStringCommand5WaitsErrorTimeout() {
  TaskScheduler scheduler;
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with string command and 5 waits with short timeout
  auto at_request =
      ex::just() |
      at::MakeRequest(at_support, "AT", at::Wait{"RESP1"}, at::Wait{"RESP2"},
                      at::Wait{"RESP3"}, at::Wait{"RESP4"}, at::Wait{"RESP5"}) |
      ex::with_timeout(TestContext{&scheduler}, 200ms);

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);
  scheduler.Update(Now());

  // Add only first 3 responses (incomplete)
  std::string_view responses[] =  // NOLINT
      {"RESP1\r\n", "RESP2\r\n", "RESP3\r\n"};
  for (auto const& resp : responses) {
    mock_serial.WriteOut(std::span{
        reinterpret_cast<std::uint8_t const*>(resp.data()), resp.size()});
  }
  // no result yet
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);

  scheduler.Update(Now() + 200ms);

  // expecting timeout
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_TRUE(s.timeout);
}

void test_AtRequestCommandMaker0WaitsSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with command maker function and 0 waits
  auto at_request = at::MakeRequest(
      ex::just(), at_support, [&]() { return at_support.SendATCommand("AT"); });

  auto res = ex::SyncWait(std::move(at_request));

  TEST_ASSERT_TRUE(res.has_value());
  TEST_ASSERT_TRUE(res->IsOk());
}

void test_AtRequestCommandMaker0WaitsError() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with command maker function and 0 waits
  auto at_request = at::MakeRequest(
      ex::just(), at_support, [&]() { return at_support.SendATCommand("AT"); });

  // Close serial port to simulate error condition
  mock_serial.close();

  auto res = ex::SyncWait(std::move(at_request));

  TEST_ASSERT_TRUE(res.has_value());
  TEST_ASSERT_TRUE(res->IsErr());
}

void test_AtRequestCommandMaker2WaitsSuccess() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with command maker and 2 waits
  auto at_request = at::MakeRequest(
      ex::just(), at_support, [&]() { return at_support.SendATCommand("AT"); },
      at::Wait{"OK"}, at::Wait{"READY"});

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  // Add first expected response "OK"
  std::string_view ok_line{"OK\r\n"};
  mock_serial.WriteOut(std::span{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // no res yet
  TEST_ASSERT_FALSE(s.res);

  std::string_view ready_line{"READY\r\n"};
  mock_serial.WriteOut(
      std::span{reinterpret_cast<std::uint8_t const*>(ready_line.data()),
                ready_line.size()});

  // got result
  TEST_ASSERT_TRUE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);
}

void test_AtRequestWithWaitHandler() {
  tests::MockSerialPort mock_serial{};
  auto at_support = AtSupport{mock_serial};

  // Create AtRequest with command maker and 2 waits
  auto at_request = at::MakeRequest(
      ex::just(), at_support, "AT",
      at::Wait{"OK", [&](auto& buffer, auto pos) -> bool {
                 std::string_view expected = "OK!";
                 auto resp = buffer.GetCrate(expected.size(), 0, pos);
                 if (resp.size() != expected.size()) {
                   return false;
                 }
                 auto resp_str = std::string_view{
                     reinterpret_cast<char const*>(resp.data()), resp.size()};
                 return resp_str == expected;
               }});

  AtReceiveState s;
  auto op = ex::connect(std::move(at_request), AtReceiver{&s});
  // start request
  ex::start(op);

  // Add expected response "OK" but it's wrong
  std::string_view ok_line_wrong{"OK\r\n"};
  mock_serial.WriteOut(
      std::span{reinterpret_cast<std::uint8_t const*>(ok_line_wrong.data()),
                ok_line_wrong.size()});

  // still wait
  TEST_ASSERT_FALSE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);

  // Add the proper response
  std::string_view ok_line{"OK!\r\n"};
  mock_serial.WriteOut(std::span{
      reinterpret_cast<std::uint8_t const*>(ok_line.data()), ok_line.size()});

  // got result
  TEST_ASSERT_TRUE(s.res);
  TEST_ASSERT_FALSE(s.error);
  TEST_ASSERT_FALSE(s.timeout);
}

}  // namespace ae::test_at_request

int test_at_request() {
  UNITY_BEGIN();
  using namespace ae::test_at_request;  // NOLINT

  RUN_TEST(test_AtRequestStringCommand0WaitsSuccess);
  RUN_TEST(test_AtRequestStringCommand0WaitsError);
  RUN_TEST(test_AtRequestStringCommand1WaitSuccess);
  RUN_TEST(test_AtRequestStringCommand1WaitErrorTimeout);
  RUN_TEST(test_AtRequestStringCommand1WaitErrorResponse);
  RUN_TEST(test_AtRequestStringCommand2WaitsSuccess);
  RUN_TEST(test_AtRequestStringCommand2WaitsErrorPartial);
  RUN_TEST(test_AtRequestStringCommand5WaitsSuccess);
  RUN_TEST(test_AtRequestStringCommand5WaitsErrorTimeout);
  RUN_TEST(test_AtRequestCommandMaker0WaitsSuccess);
  RUN_TEST(test_AtRequestCommandMaker0WaitsError);
  RUN_TEST(test_AtRequestCommandMaker2WaitsSuccess);
  RUN_TEST(test_AtRequestWithWaitHandler);
  return UNITY_END();
}
