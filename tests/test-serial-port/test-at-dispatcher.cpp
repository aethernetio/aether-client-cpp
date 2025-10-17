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

#include "aether/serial_ports/at_support/at_buffer.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"

#include "tests/test-serial-port/mock-serial-port.h"
#include "tests/test-serial-port/mock-at-observer.h"

namespace ae::test_at_dispatcher {
void test_AtDispatcherRegisterSingleObserver() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  tests::MockAtObserver observer{};
  int observation_count = 0;
  AtBuffer* observed_buffer = nullptr;
  AtBuffer::iterator observed_pos{};

  observer.SetObservationCallback(
      [&](AtBuffer& buffer, AtBuffer::iterator pos) {
        observation_count++;
        observed_buffer = &buffer;
        observed_pos = pos;
      });

  // Register observer for "OK" command
  dispatcher.Listen("OK", &observer);

  // Add "OK" to buffer to trigger observer
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify observer was called exactly once
  TEST_ASSERT_EQUAL(1, observation_count);

  // Verify observer was called with correct parameters
  TEST_ASSERT_FALSE(observed_pos == buffer.end());

  // Verify the observed position contains "OK"
  std::string_view observed_content(
      reinterpret_cast<char const*>(observed_pos->data()),
      observed_pos->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", observed_content.data(),
                               observed_content.size());
}

void test_AtDispatcherRegisterMultipleObservers() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  tests::MockAtObserver observer1{};
  tests::MockAtObserver observer2{};
  tests::MockAtObserver observer3{};
  int observation_count1 = 0;
  int observation_count2 = 0;
  int observation_count3 = 0;
  AtBuffer::iterator observed_pos1{};
  AtBuffer::iterator observed_pos2{};

  observer1.SetObservationCallback([&](AtBuffer&, AtBuffer::iterator pos) {
    observation_count1++;
    observed_pos1 = pos;
  });

  observer2.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observation_count2++; });

  observer3.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observation_count3++; });

  // Register observers for different commands
  dispatcher.Listen("OK", &observer1);
  dispatcher.Listen("ERROR", &observer2);
  dispatcher.Listen("READY", &observer3);

  // Add "OK" to buffer to trigger observer1
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify only observer1 was called
  TEST_ASSERT_EQUAL(1, observation_count1);
  TEST_ASSERT_EQUAL(0, observation_count2);
  TEST_ASSERT_EQUAL(0, observation_count3);

  // Verify observer1 was called with correct parameters
  TEST_ASSERT_FALSE(observed_pos1 == buffer.end());

  // Verify the observed position contains "OK"
  std::string_view observed_content1(
      reinterpret_cast<char const*>(observed_pos1->data()),
      observed_pos1->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", observed_content1.data(),
                               observed_content1.size());

  // Reset observation counts and test with "ERROR"
  observation_count1 = 0;
  observation_count2 = 0;
  observation_count3 = 0;

  observer2.SetObservationCallback([&](AtBuffer&, AtBuffer::iterator pos) {
    observation_count2++;
    observed_pos2 = pos;
  });

  DataBuffer data2;
  std::string_view line2{"ERROR\r\n"};
  data2.insert(data2.end(), line2.begin(), line2.end());
  mock_serial.WriteOut(data2);

  // Verify only observer2 was called
  TEST_ASSERT_EQUAL(0, observation_count1);
  TEST_ASSERT_EQUAL(1, observation_count2);
  TEST_ASSERT_EQUAL(0, observation_count3);

  // Verify observer2 was called with correct parameters
  TEST_ASSERT_FALSE(observed_pos2 == buffer.end());

  // Verify the observed position contains "ERROR"
  std::string_view observed_content2(
      reinterpret_cast<char const*>(observed_pos2->data()),
      observed_pos2->size());
  TEST_ASSERT_EQUAL_STRING_LEN("ERROR", observed_content2.data(),
                               observed_content2.size());
}

void test_AtDispatcherRegisterSameCommandMultipleTimes() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  tests::MockAtObserver observer1{};
  tests::MockAtObserver observer2{};
  int observation_count1 = 0;
  int observation_count2 = 0;
  AtBuffer::iterator observed_pos{};

  observer1.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observation_count1++; });

  observer2.SetObservationCallback([&](AtBuffer&, AtBuffer::iterator pos) {
    observation_count2++;
    observed_pos = pos;
  });

  // Register observer1 for "OK" command
  dispatcher.Listen("OK", &observer1);

  // Register observer2 for the same "OK" command
  dispatcher.Listen("OK", &observer2);

  // Add "OK" to buffer to trigger observer
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify only observer2 was called (last one wins)
  TEST_ASSERT_EQUAL(0, observation_count1);
  TEST_ASSERT_EQUAL(1, observation_count2);

  // Verify observer2 was called with correct parameters
  TEST_ASSERT_FALSE(observed_pos == buffer.end());

  // Verify the observed position contains "OK"
  std::string_view observed_content(
      reinterpret_cast<char const*>(observed_pos->data()),
      observed_pos->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", observed_content.data(),
                               observed_content.size());
}

void test_AtDispatcherRemoveObserver() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  tests::MockAtObserver observer{};
  int observation_count = 0;

  observer.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observation_count++; });

  // Register observer for "OK" command
  dispatcher.Listen("OK", &observer);

  // Remove observer
  dispatcher.Remove(&observer);

  // Add "OK" to buffer
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify observer was not called after removal
  TEST_ASSERT_EQUAL(0, observation_count);
}

void test_AtDispatcherRemoveObserverMultipleCommands() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  tests::MockAtObserver observer{};
  int observation_count = 0;

  observer.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observation_count++; });

  // Register observer for multiple commands
  dispatcher.Listen("OK", &observer);
  dispatcher.Listen("ERROR", &observer);

  // Remove observer
  dispatcher.Remove(&observer);

  // Add "OK" and "ERROR" to buffer
  DataBuffer data;
  std::string_view lines{"OK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Verify observer was not called after removal from all commands
  TEST_ASSERT_EQUAL(0, observation_count);
}

void test_AtDispatcherComplexPatternScenarios() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};

  // Test overlapping and complex pattern scenarios
  tests::MockAtObserver observer1{};
  tests::MockAtObserver observer2{};
  int observer1_call_count = 0;
  int observer2_call_count = 0;

  observer1.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observer1_call_count++; });

  observer2.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observer2_call_count++; });

  // Register observers for overlapping patterns
  dispatcher.Listen("AT", &observer1);   // Short pattern
  dispatcher.Listen("AT+", &observer2);  // Longer pattern that starts with "AT"

  // Test with "AT" - should trigger observer1 only
  DataBuffer data1;
  std::string_view line1{"AT\r\n"};
  data1.insert(data1.end(), line1.begin(), line1.end());
  mock_serial.WriteOut(data1);

  TEST_ASSERT_EQUAL(1, observer1_call_count);
  TEST_ASSERT_EQUAL(0, observer2_call_count);

  // Reset and test with "AT+" - should trigger both observers due to substring
  // matching
  observer1_call_count = 0;
  observer2_call_count = 0;

  DataBuffer data2;
  std::string_view line2{"AT+\r\n"};
  data2.insert(data2.end(), line2.begin(), line2.end());
  mock_serial.WriteOut(data2);

  TEST_ASSERT_EQUAL(1, observer1_call_count);
  TEST_ASSERT_EQUAL(1, observer2_call_count);

  // Test overlapping patterns with same starting characters
  // Test overlapping patterns with same starting characters
  observer1_call_count = 0;
  observer2_call_count = 0;

  // Re-register with different patterns to test overlapping behavior
  dispatcher.Remove(&observer1);
  dispatcher.Remove(&observer2);

  dispatcher.Listen("AT+CGMI", &observer1);  // Specific manufacturer ID
  dispatcher.Listen("AT+CG", &observer2);    // Generic CG command prefix

  DataBuffer data3;
  std::string_view line3{"AT+CGMI\r\n"};
  data3.insert(data3.end(), line3.begin(), line3.end());
  mock_serial.WriteOut(data3);

  // Both patterns match "AT+CGMI" - both observers should be called
  // because FindPattern uses substring matching
  TEST_ASSERT_EQUAL(1, observer1_call_count);
  TEST_ASSERT_EQUAL(1, observer2_call_count);

  // Test with partial match that should trigger generic pattern only
  observer1_call_count = 0;
  observer2_call_count = 0;

  DataBuffer data4;
  std::string_view line4{"AT+CGMM\r\n"};
  data4.insert(data4.end(), line4.begin(), line4.end());
  mock_serial.WriteOut(data4);

  // Only AT+CG pattern matches "AT+CGMM" - AT+CGMI doesn't match
  TEST_ASSERT_EQUAL(0, observer1_call_count);
  TEST_ASSERT_EQUAL(1, observer2_call_count);
}

}  // namespace ae::test_at_dispatcher

int test_at_dispatcher() {
  UNITY_BEGIN();

  RUN_TEST(ae::test_at_dispatcher::test_AtDispatcherRegisterSingleObserver);
  RUN_TEST(ae::test_at_dispatcher::test_AtDispatcherRegisterMultipleObservers);
  RUN_TEST(ae::test_at_dispatcher::
               test_AtDispatcherRegisterSameCommandMultipleTimes);
  RUN_TEST(ae::test_at_dispatcher::test_AtDispatcherRemoveObserver);
  RUN_TEST(
      ae::test_at_dispatcher::test_AtDispatcherRemoveObserverMultipleCommands);

  RUN_TEST(ae::test_at_dispatcher::test_AtDispatcherComplexPatternScenarios);

  return UNITY_END();
}
