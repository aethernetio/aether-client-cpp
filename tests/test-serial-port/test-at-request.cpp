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

#include "aether/actions/action_processor.h"
#include "aether/serial_ports/at_support/at_request.h"
#include "aether/serial_ports/at_support/at_support.h"

#include "tests/test-serial-port/mock-serial-port.h"

namespace ae::test_at_request {

void ProcessActions(ActionProcessor& ap, std::size_t count = 10,
                    Duration step = {}) {
  auto current_time = Now();
  for (int i = 0; i < count; i++) {
    ap.Update(current_time += step);
  }
}

void test_AtRequestStringCommand0WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 0 waits
  auto at_request = at_support.MakeRequest("AT");

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand0WaitsError() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 0 waits
  auto at_request = at_support.MakeRequest("AT");

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Close serial port to simulate error condition
  mock_serial.close();

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify error result (command executes but ERROR response received)
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand1WaitSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 1 wait for "OK"
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add expected response "OK" to trigger wait observer
  ae::DataBuffer data;
  std::string_view ok_line{"OK\r\n"};
  data.insert(data.end(), ok_line.begin(), ok_line.end());
  mock_serial.WriteOut(data);

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand1WaitErrorTimeout() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 1 wait with short timeout
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::milliseconds(100)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Don't send expected response - let it timeout
  // Process with time advancing beyond timeout
  ProcessActions(ap, 10, std::chrono::milliseconds(200));

  // Verify timeout error result
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand1WaitErrorResponse() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 1 wait
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add ERROR response to trigger error observer
  ae::DataBuffer data;
  std::string_view error_line{"ERROR\r\n"};
  data.insert(data.end(), error_line.begin(), error_line.end());
  mock_serial.WriteOut(data);

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify error result
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand2WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 2 waits
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::seconds(5)},
      AtRequest::Wait{"READY", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add first expected response "OK"
  ae::DataBuffer data1;
  std::string_view ok_line{"OK\r\n"};
  data1.insert(data1.end(), ok_line.begin(), ok_line.end());
  mock_serial.WriteOut(data1);

  // Process to handle first response
  ap.Update(Now());

  // Add second expected response "READY"
  ae::DataBuffer data2;
  std::string_view ready_line{"READY\r\n"};
  data2.insert(data2.end(), ready_line.begin(), ready_line.end());
  mock_serial.WriteOut(data2);

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand2WaitsErrorPartial() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 2 waits with short timeout
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::milliseconds(100)},
      AtRequest::Wait{"READY", std::chrono::milliseconds(100)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add only first expected response "OK"
  ae::DataBuffer data;
  std::string_view ok_line{"OK\r\n"};
  data.insert(data.end(), ok_line.begin(), ok_line.end());
  mock_serial.WriteOut(data);

  // Process with time advancing beyond timeout for second wait
  ProcessActions(ap, 10, std::chrono::milliseconds{200});

  // Verify timeout error result (only one response received)
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand3WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 3 waits
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"OK", std::chrono::seconds(5)},
      AtRequest::Wait{"READY", std::chrono::seconds(5)},
      AtRequest::Wait{"DONE", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add all expected responses
  ae::DataBuffer data1;
  std::string_view ok_line{"OK\r\n"};
  data1.insert(data1.end(), ok_line.begin(), ok_line.end());
  mock_serial.WriteOut(data1);

  ae::DataBuffer data2;
  std::string_view ready_line{"READY\r\n"};
  data2.insert(data2.end(), ready_line.begin(), ready_line.end());
  mock_serial.WriteOut(data2);

  ae::DataBuffer data3;
  std::string_view done_line{"DONE\r\n"};
  data3.insert(data3.end(), done_line.begin(), done_line.end());
  mock_serial.WriteOut(data3);

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand5WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 5 waits
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"RESP1", std::chrono::seconds(5)},
      AtRequest::Wait{"RESP2", std::chrono::seconds(5)},
      AtRequest::Wait{"RESP3", std::chrono::seconds(5)},
      AtRequest::Wait{"RESP4", std::chrono::seconds(5)},
      AtRequest::Wait{"RESP5", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add all expected responses
  const char* responses[] = {"RESP1", "RESP2", "RESP3", "RESP4", "RESP5"};
  for (const char* resp : responses) {
    ae::DataBuffer data;
    std::string line_str = std::string(resp) + "\r\n";
    std::string_view line = line_str;
    data.insert(data.end(), line.begin(), line.end());
    mock_serial.WriteOut(data);
    ap.Update(Now());
  }

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestStringCommand5WaitsErrorTimeout() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with string command and 5 waits with short timeout
  auto at_request = at_support.MakeRequest(
      "AT", AtRequest::Wait{"RESP1", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP2", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP3", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP4", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP5", std::chrono::milliseconds(100)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add only first 3 responses (incomplete)
  const char* responses[] = {"RESP1", "RESP2", "RESP3"};
  for (const char* resp : responses) {
    ae::DataBuffer data;
    std::string line_str = std::string(resp) + "\r\n";
    std::string_view line = line_str;
    data.insert(data.end(), line.begin(), line.end());
    mock_serial.WriteOut(data);
    ap.Update(Now());
  }

  // Process with time advancing beyond timeout for remaining waits
  ProcessActions(ap, 10, std::chrono::milliseconds{200});

  // Verify timeout error result (incomplete responses)
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestCommandMaker0WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with command maker function and 0 waits
  auto at_request =
      at_support.MakeRequest([&]() { return at_support.SendATCommand("AT"); });

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestCommandMaker0WaitsError() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Close serial port to simulate error condition
  mock_serial.close();

  // Create AtRequest with command maker - should return null due to closed port
  auto at_request = at_support.MakeRequest("AT");

  // Closed serial port should result in null request
  TEST_ASSERT_FALSE(static_cast<bool>(at_request));
}

void test_AtRequestCommandMaker2WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with command maker and 2 waits
  auto at_request =
      at_support.MakeRequest([&]() { return at_support.SendATCommand("AT"); },
                             AtRequest::Wait{"OK", std::chrono::seconds(5)},
                             AtRequest::Wait{"READY", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add expected responses
  ae::DataBuffer data1;
  std::string_view ok_line{"OK\r\n"};
  data1.insert(data1.end(), ok_line.begin(), ok_line.end());
  mock_serial.WriteOut(data1);

  ae::DataBuffer data2;
  std::string_view ready_line{"READY\r\n"};
  data2.insert(data2.end(), ready_line.begin(), ready_line.end());
  mock_serial.WriteOut(data2);

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestCommandMaker3WaitsError() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with command maker and 3 waits with short timeout
  auto at_request = at_support.MakeRequest(
      [&]() { return at_support.SendATCommand("AT"); },
      AtRequest::Wait{"RESP1", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP2", std::chrono::milliseconds(100)},
      AtRequest::Wait{"RESP3", std::chrono::milliseconds(100)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add only first response
  ae::DataBuffer data;
  std::string_view resp1_line{"RESP1\r\n"};
  data.insert(data.end(), resp1_line.begin(), resp1_line.end());
  mock_serial.WriteOut(data);

  // Process with time advancing beyond timeout
  ProcessActions(ap, 10, std::chrono::milliseconds{200});

  // Verify timeout error result
  TEST_ASSERT_FALSE(result_triggered);
  TEST_ASSERT_TRUE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

void test_AtRequestCommandMaker5WaitsSuccess() {
  auto ap = ActionProcessor{};
  tests::MockSerialPort mock_serial{};
  AtSupport at_support{ap, mock_serial};

  // Create AtRequest with command maker and 5 waits
  auto at_request =
      at_support.MakeRequest([&]() { return at_support.SendATCommand("AT"); },
                             AtRequest::Wait{"STEP1", std::chrono::seconds(5)},
                             AtRequest::Wait{"STEP2", std::chrono::seconds(5)},
                             AtRequest::Wait{"STEP3", std::chrono::seconds(5)},
                             AtRequest::Wait{"STEP4", std::chrono::seconds(5)},
                             AtRequest::Wait{"STEP5", std::chrono::seconds(5)});

  TEST_ASSERT_TRUE(static_cast<bool>(at_request));

  // Subscribe to status events
  bool result_triggered = false;
  bool error_triggered = false;
  bool stop_triggered = false;

  at_request->StatusEvent().Subscribe([&](auto status) {
    status.OnResult([&]() { result_triggered = true; })
        .OnError([&]() { error_triggered = true; })
        .OnStop([&]() { stop_triggered = true; });
  });

  // Process the action to start command execution
  ap.Update(Now());

  // Add all expected responses
  const char* responses[] = {"STEP1", "STEP2", "STEP3", "STEP4", "STEP5"};
  for (const char* resp : responses) {
    ae::DataBuffer data;
    std::string line_str = std::string(resp) + "\r\n";
    std::string_view line = line_str;
    data.insert(data.end(), line.begin(), line.end());
    mock_serial.WriteOut(data);
    ap.Update(Now());
  }

  // Process the action multiple times to ensure completion
  ProcessActions(ap);

  // Verify success result
  TEST_ASSERT_TRUE(result_triggered);
  TEST_ASSERT_FALSE(error_triggered);
  TEST_ASSERT_FALSE(stop_triggered);
}

}  // namespace ae::test_at_request

int test_at_request() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand0WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand0WaitsError);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand1WaitSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand1WaitErrorTimeout);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand1WaitErrorResponse);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand2WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand2WaitsErrorPartial);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand3WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand5WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestStringCommand5WaitsErrorTimeout);
  RUN_TEST(ae::test_at_request::test_AtRequestCommandMaker0WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestCommandMaker0WaitsError);
  RUN_TEST(ae::test_at_request::test_AtRequestCommandMaker2WaitsSuccess);
  RUN_TEST(ae::test_at_request::test_AtRequestCommandMaker3WaitsError);
  RUN_TEST(ae::test_at_request::test_AtRequestCommandMaker5WaitsSuccess);
  return UNITY_END();
}
