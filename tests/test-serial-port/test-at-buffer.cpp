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
#include "tests/test-serial-port/mock-serial-port.h"

namespace ae::tests {}  // namespace ae::tests

void test_AtBufferSingleCompleteLine() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Send a single complete line with \r\n
  ae::DataBuffer data;
  std::string_view line{"AT\r\n"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Verify buffer contains the line without \r\n
  TEST_ASSERT_FALSE(buffer.begin() == buffer.end());
  auto it = buffer.begin();
  TEST_ASSERT_EQUAL(2, it->size());  // "AT" without \r\n
  std::string_view line_content(reinterpret_cast<char const*>(it->data()),
                                it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("AT", line_content.data(), line_content.size());
}

void test_AtBufferMultipleCompleteLines() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Send multiple complete lines with \r\n
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Verify buffer contains all lines without \r\n
  TEST_ASSERT_FALSE(buffer.begin() == buffer.end());

  auto it = buffer.begin();
  TEST_ASSERT_EQUAL(2, it->size());  // "AT" without \r\n
  std::string_view line1(reinterpret_cast<char const*>(it->data()), it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("AT", line1.data(), line1.size());

  ++it;
  TEST_ASSERT_EQUAL(2, it->size());  // "OK" without \r\n
  std::string_view line2(reinterpret_cast<char const*>(it->data()), it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", line2.data(), line2.size());

  ++it;
  TEST_ASSERT_EQUAL(5, it->size());  // "ERROR" without \r\n
  std::string_view line3(reinterpret_cast<char const*>(it->data()), it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("ERROR", line3.data(), line3.size());
}

void test_AtBufferIncompleteLine() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Send incomplete line without \r\n
  ae::DataBuffer data;
  std::string_view line{"AT"};
  data.insert(data.end(), line.begin(), line.end());
  mock_serial.WriteOut(data);

  // Note: Current implementation stores incomplete lines
  // Verify buffer contains the incomplete line
  TEST_ASSERT_FALSE(buffer.begin() == buffer.end());
  auto it = buffer.begin();
  TEST_ASSERT_EQUAL(2, it->size());  // "AT" without \r\n
  std::string_view line_content(reinterpret_cast<char const*>(it->data()),
                                it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("AT", line_content.data(), line_content.size());
}

void test_AtBufferFindPatternBasic() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test finding pattern that exists
  auto found_it = buffer.FindPattern(std::string_view{"OK"});
  TEST_ASSERT_FALSE(found_it == buffer.end());

  std::string_view found_content(
      reinterpret_cast<char const*>(found_it->data()), found_it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", found_content.data(),
                               found_content.size());

  // Test finding pattern that doesn't exist
  auto not_found_it = buffer.FindPattern(std::string_view{"NOTFOUND"});
  TEST_ASSERT_TRUE(not_found_it == buffer.end());
}

void test_AtBufferFindPatternWithStartIterator() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test finding pattern from start iterator
  auto it = buffer.begin();
  ++it;  // Move to second line "OK"

  // Search for "ERROR" starting from position after "OK"
  auto found_it = buffer.FindPattern(std::string_view{"ERROR"}, it);
  TEST_ASSERT_FALSE(found_it == buffer.end());

  std::string_view found_content(
      reinterpret_cast<char const*>(found_it->data()), found_it->size());
  TEST_ASSERT_EQUAL_STRING_LEN("ERROR", found_content.data(),
                               found_content.size());

  // Test finding pattern that doesn't exist from start iterator
  auto not_found_it = buffer.FindPattern(std::string_view{"AT"}, it);
  TEST_ASSERT_TRUE(not_found_it == buffer.end());
}

void test_AtBufferGetCrateBasic() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test GetCrate with exact size
  auto crate_data = buffer.GetCrate(2);  // Request 2 bytes
  TEST_ASSERT_EQUAL(2, crate_data.size());
  std::string_view crate_content(
      reinterpret_cast<char const*>(crate_data.data()), crate_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN("AT", crate_content.data(),
                               crate_content.size());
}

void test_AtBufferGetCrateWithOffset() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test GetCrate with offset - skip first byte of first line
  auto crate_data =
      buffer.GetCrate(2, 1);  // Request 2 bytes starting from offset 1
  TEST_ASSERT_EQUAL(2, crate_data.size());
  std::string_view crate_content(
      reinterpret_cast<char const*>(crate_data.data()), crate_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN("TO", crate_content.data(),
                               crate_content.size());
}

void test_AtBufferGetCrateWithStartIterator() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test GetCrate with start iterator - start from second line
  auto it = buffer.begin();
  ++it;  // Move to second line "OK"

  auto crate_data =
      buffer.GetCrate(2, 0, it);  // Request 2 bytes from "OK" line
  TEST_ASSERT_EQUAL(2, crate_data.size());
  std::string_view crate_content(
      reinterpret_cast<char const*>(crate_data.data()), crate_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN("OK", crate_content.data(),
                               crate_content.size());
}

void test_AtBufferGetCrateWithStartIteratorAndOffset() {
  ae::tests::MockSerialPort mock_serial{};
  ae::AtBuffer buffer{mock_serial};

  // Add some data to the buffer
  ae::DataBuffer data;
  std::string_view lines{"AT\r\nOK\r\nERROR\r\n"};
  data.insert(data.end(), lines.begin(), lines.end());
  mock_serial.WriteOut(data);

  // Test GetCrate with start iterator and offset
  auto it = buffer.begin();
  ++it;  // Move to second line "OK"

  auto crate_data = buffer.GetCrate(
      1, 1, it);  // Request 1 byte from "OK" line starting at offset 1
  TEST_ASSERT_EQUAL(1, crate_data.size());
  std::string_view crate_content(
      reinterpret_cast<char const*>(crate_data.data()), crate_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN("K", crate_content.data(), crate_content.size());
}

int test_at_buffer() {
  UNITY_BEGIN();

  RUN_TEST(test_AtBufferSingleCompleteLine);
  RUN_TEST(test_AtBufferMultipleCompleteLines);
  RUN_TEST(test_AtBufferIncompleteLine);
  RUN_TEST(test_AtBufferFindPatternBasic);
  RUN_TEST(test_AtBufferFindPatternWithStartIterator);
  RUN_TEST(test_AtBufferGetCrateBasic);
  RUN_TEST(test_AtBufferGetCrateWithOffset);
  RUN_TEST(test_AtBufferGetCrateWithStartIterator);
  RUN_TEST(test_AtBufferGetCrateWithStartIteratorAndOffset);

  return UNITY_END();
}
