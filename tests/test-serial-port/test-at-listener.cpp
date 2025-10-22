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

#include "aether/serial_ports/at_support/at_listener.h"
#include "aether/serial_ports/at_support/at_dispatcher.h"
#include "aether/serial_ports/at_support/at_buffer.h"
#include "tests/test-serial-port/mock-serial-port.h"
#include "tests/test-serial-port/mock-at-observer.h"

namespace ae::test_at_listener {

void test_AtListenerConstructorRegistersWithDispatcher() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler_call_count = 0;
  AtBuffer::iterator observed_pos{};

  // Create AtListener with dispatcher and command
  AtListener listener{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator pos) {
                        handler_call_count++;
                        observed_pos = pos;
                      }};

  // Add matching pattern to buffer to trigger handler
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify handler was called (proves registration worked)
  TEST_ASSERT_EQUAL(1, handler_call_count);

  TEST_ASSERT_FALSE(observed_pos == buffer.end());

  // Verify the observed position contains "OK"
  std::string_view observed_content(
      reinterpret_cast<char const*>(observed_pos->data()),
      observed_pos->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", observed_content.data(),
                               observed_content.size());
}

void test_AtListenerStoresHandlerCorrectly() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler_call_count = 0;
  AtBuffer::iterator observed_pos{};

  // Create AtListener with specific handler
  AtListener listener{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator pos) {
                        handler_call_count++;
                        observed_pos = pos;
                      }};

  // Trigger observation
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify handler was called with correct parameters
  TEST_ASSERT_EQUAL(1, handler_call_count);

  TEST_ASSERT_FALSE(observed_pos == buffer.end());

  // Verify the observed position contains "OK"
  std::string_view observed_content(
      reinterpret_cast<char const*>(observed_pos->data()),
      observed_pos->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", observed_content.data(),
                               observed_content.size());
}

void test_AtListenerMultipleListenersDifferentPatterns() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler1_call_count = 0;
  int handler2_call_count = 0;
  int handler3_call_count = 0;

  // Create multiple listeners with different patterns
  AtListener listener1{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator) {
                         handler1_call_count++;
                       }};
  AtListener listener2{dispatcher, "ERROR", [&](AtBuffer&, AtBuffer::iterator) {
                         handler2_call_count++;
                       }};
  AtListener listener3{dispatcher, "READY", [&](AtBuffer&, AtBuffer::iterator) {
                         handler3_call_count++;
                       }};

  // Add data matching "OK" pattern
  DataBuffer data1;
  std::string_view line1{"OK\r\n"};
  data1.insert(data1.end(), line1.begin(), line1.end());
  mock_serial.WriteOut(data1);

  // Verify only handler1 was called
  TEST_ASSERT_EQUAL(1, handler1_call_count);
  TEST_ASSERT_EQUAL(0, handler2_call_count);
  TEST_ASSERT_EQUAL(0, handler3_call_count);

  // Reset counts and test with "ERROR"
  handler1_call_count = 0;
  handler2_call_count = 0;
  handler3_call_count = 0;

  DataBuffer data2;
  std::string_view line2{"ERROR\r\n"};
  data2.insert(data2.end(), line2.begin(), line2.end());
  mock_serial.WriteOut(data2);

  // Verify only handler2 was called
  TEST_ASSERT_EQUAL(0, handler1_call_count);
  TEST_ASSERT_EQUAL(1, handler2_call_count);
  TEST_ASSERT_EQUAL(0, handler3_call_count);

  // Reset counts and test with "READY"
  handler1_call_count = 0;
  handler2_call_count = 0;
  handler3_call_count = 0;

  DataBuffer data3;
  std::string_view line3{"READY\r\n"};
  data3.insert(data3.end(), line3.begin(), line3.end());
  mock_serial.WriteOut(data3);

  // Verify only handler3 was called
  TEST_ASSERT_EQUAL(0, handler1_call_count);
  TEST_ASSERT_EQUAL(0, handler2_call_count);
  TEST_ASSERT_EQUAL(1, handler3_call_count);
}

void test_AtListenerSingleListenerMultipleMatch() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler_call_count = 0;

  // Create multiple listeners with different patterns
  AtListener listener1{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator) {
                         handler_call_count++;
                       }};

  // Add data matching "OK" pattern several times
  DataBuffer data1;
  std::string_view line1{"OK\r\nOK\r\nOK\r\n"};
  data1.insert(data1.end(), line1.begin(), line1.end());
  mock_serial.WriteOut(data1);

  // Verify handler was called 3 times
  TEST_ASSERT_EQUAL(3, handler_call_count);
}

void test_AtListenerDestructorUnregistersFromDispatcher() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler_call_count = 0;

  // Create listener in scope
  {
    AtListener listener{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator) {
                          handler_call_count++;
                        }};

    // Add matching pattern to buffer while listener exists
    DataBuffer data;
    std::string_view line{"OK\r\n"};
    data.insert(data.end(), line.begin(), line.end());
    mock_serial.WriteOut(data);

    // Verify handler was called while listener exists
    TEST_ASSERT_EQUAL(1, handler_call_count);
  }  // Listener destroyed here, should unregister

  // Reset count and add matching pattern again after destruction
  handler_call_count = 0;
  DataBuffer data2;
  std::string_view line2{"OK\r\n"};
  data2.insert(data2.end(), line2.begin(), line2.end());
  mock_serial.WriteOut(data2);

  // Verify handler was NOT called after destruction (proves unregistration
  // worked)
  TEST_ASSERT_EQUAL(0, handler_call_count);
}

void test_AtListenerNoHandlerCallsAfterDestruction() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int handler_call_count = 0;

  // Create and immediately destroy listener
  {
    AtListener listener{dispatcher, "OK", [&](AtBuffer&, AtBuffer::iterator) {
                          handler_call_count++;
                        }};
  }  // Listener destroyed immediately

  // Add matching pattern to buffer after destruction
  DataBuffer data;
  std::string_view line{"OK\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify handler was not called after destruction
  TEST_ASSERT_EQUAL(0, handler_call_count);
}

void test_AtListenerRAIIBehavior() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};
  int outer_handler_count = 0;
  int inner_handler_count = 0;

  // Outer scope with listener
  {
    AtListener outer_listener{
        dispatcher, "OK",
        [&](AtBuffer&, AtBuffer::iterator) { outer_handler_count++; }};

    // Add matching pattern while outer listener exists
    DataBuffer data1;
    std::string_view line1{"OK\r\n"};
    data1.insert(data1.end(), line1.begin(), line1.end());
    mock_serial.WriteOut(data1);

    // Verify outer handler was called
    TEST_ASSERT_EQUAL(1, outer_handler_count);
    TEST_ASSERT_EQUAL(0, inner_handler_count);

    // Inner scope with different listener
    {
      AtListener inner_listener{
          dispatcher, "ERROR",
          [&](AtBuffer&, AtBuffer::iterator) { inner_handler_count++; }};

      // Add pattern matching inner listener
      DataBuffer data2;
      std::string_view line2{"ERROR\r\n"};
      data2.insert(data2.end(), line2.begin(), line2.end());
      mock_serial.WriteOut(data2);

      // Verify both handlers were called
      TEST_ASSERT_EQUAL(1, outer_handler_count);
      TEST_ASSERT_EQUAL(1, inner_handler_count);

      // Add pattern matching outer listener again
      DataBuffer data3;
      std::string_view line3{"OK\r\n"};
      data3.insert(data3.end(), line3.begin(), line3.end());
      mock_serial.WriteOut(data3);

      // Verify outer handler was called again
      TEST_ASSERT_EQUAL(2, outer_handler_count);
      TEST_ASSERT_EQUAL(1, inner_handler_count);
    }  // Inner listener destroyed here

    // Add pattern matching inner listener after destruction
    outer_handler_count = 0;
    inner_handler_count = 0;
    DataBuffer data4;
    std::string_view line4{"ERROR\r\n"};
    data4.insert(data4.end(), line4.begin(), line4.end());
    mock_serial.WriteOut(data4);

    // Verify only outer handler was called (inner listener is gone)
    TEST_ASSERT_EQUAL(0, outer_handler_count);
    TEST_ASSERT_EQUAL(0, inner_handler_count);

    // Add pattern matching outer listener
    DataBuffer data5;
    std::string_view line5{"OK\r\n"};
    data5.insert(data5.end(), line5.begin(), line5.end());
    mock_serial.WriteOut(data5);

    // Verify outer handler was called
    TEST_ASSERT_EQUAL(1, outer_handler_count);
    TEST_ASSERT_EQUAL(0, inner_handler_count);
  }  // Outer listener destroyed here

  // Add pattern matching outer listener after both are destroyed
  outer_handler_count = 0;
  inner_handler_count = 0;
  DataBuffer data6;
  std::string_view line6{"OK\r\n"};
  data6.insert(data6.end(), line6.begin(), line6.end());
  mock_serial.WriteOut(data6);

  // Verify no handlers were called (both listeners are gone)
  TEST_ASSERT_EQUAL(0, outer_handler_count);
  TEST_ASSERT_EQUAL(0, inner_handler_count);
}

void test_AtListenerIntegrationWithObserver() {
  tests::MockSerialPort mock_serial{};
  AtBuffer buffer{mock_serial};
  AtDispatcher dispatcher{buffer};

  // Test mixed usage of observers and listeners
  tests::MockAtObserver observer{};
  int observer_call_count = 0;
  int listener_call_count = 0;

  observer.SetObservationCallback(
      [&](AtBuffer&, AtBuffer::iterator) { observer_call_count++; });

  // Register observer for "OK" command
  dispatcher.Listen("OK", &observer);

  // Create listener for "ERROR" command
  AtListener listener{dispatcher, "ERROR", [&](AtBuffer&, AtBuffer::iterator) {
                        listener_call_count++;
                      }};

  // Add "OK" to buffer - should trigger observer only
  DataBuffer data1;
  std::string_view line1{"OK\r\n"};
  data1.insert(data1.end(), line1.begin(), line1.end());
  mock_serial.WriteOut(data1);

  // Verify only observer was called
  TEST_ASSERT_EQUAL(1, observer_call_count);
  TEST_ASSERT_EQUAL(0, listener_call_count);

  // Reset counts and test with "ERROR" - should trigger listener only
  observer_call_count = 0;
  listener_call_count = 0;

  DataBuffer data2;
  std::string_view line2{"ERROR\r\n"};
  data2.insert(data2.end(), line2.begin(), line2.end());
  mock_serial.WriteOut(data2);

  // Verify only listener was called
  TEST_ASSERT_EQUAL(0, observer_call_count);
  TEST_ASSERT_EQUAL(1, listener_call_count);

  // Test both patterns in same data
  observer_call_count = 0;
  listener_call_count = 0;

  DataBuffer data3;
  std::string_view line3{"OK\r\nERROR\r\n"};
  data3.insert(data3.end(), line3.begin(), line3.end());
  mock_serial.WriteOut(data3);

  // Verify both were called
  TEST_ASSERT_EQUAL(1, observer_call_count);
  TEST_ASSERT_EQUAL(1, listener_call_count);

  // Test listener destruction - should not affect observer
  {
    AtListener temp_listener{dispatcher, "READY",
                             [&](AtBuffer&, AtBuffer::iterator) {}};
  }  // temp_listener destroyed

  observer_call_count = 0;
  listener_call_count = 0;

  DataBuffer data4;
  std::string_view line4{"OK\r\n"};
  data4.insert(data4.end(), line4.begin(), line4.end());
  mock_serial.WriteOut(data4);

  // Verify observer still works after listener destruction
  TEST_ASSERT_EQUAL(1, observer_call_count);
  TEST_ASSERT_EQUAL(0, listener_call_count);
}

}  // namespace ae::test_at_listener

int test_at_listener() {
  UNITY_BEGIN();

  RUN_TEST(
      ae::test_at_listener::test_AtListenerConstructorRegistersWithDispatcher);
  RUN_TEST(ae::test_at_listener::test_AtListenerStoresHandlerCorrectly);
  RUN_TEST(
      ae::test_at_listener::test_AtListenerMultipleListenersDifferentPatterns);
  RUN_TEST(ae::test_at_listener::test_AtListenerSingleListenerMultipleMatch);
  RUN_TEST(
      ae::test_at_listener::test_AtListenerDestructorUnregistersFromDispatcher);
  RUN_TEST(ae::test_at_listener::test_AtListenerNoHandlerCallsAfterDestruction);
  RUN_TEST(ae::test_at_listener::test_AtListenerRAIIBehavior);
  RUN_TEST(ae::test_at_listener::test_AtListenerIntegrationWithObserver);

  return UNITY_END();
}
