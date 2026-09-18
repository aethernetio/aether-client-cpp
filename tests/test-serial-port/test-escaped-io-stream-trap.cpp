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

#include <array>
#include <cstdint>
#include <sstream>

#include <unity.h>

#include "aether/escaped_io_stream_trap.h"

namespace ae::test_escaped_io_stream_trap {
class BlobCollector final : public tele::ILogCollector {
 public:
  void WriteLine(tele::ILogLine& log_line) override { log_line.Blob(data); }

  std::array<std::uint8_t, 8> data{'A', '\r', '\n', 'B', '\t', 0x00, 0x7F, 'C'};
};

void test_ControlCharactersAreRenderedAsHex() {
  std::ostringstream stream;
  auto trap = tele::EscapedIoStreamTrap{stream};
  auto collector = BlobCollector{};
  constexpr auto module = tele::Module{1, 0, 1, "Test"};
  auto const tag = tele::Tag{0, module, "Test"};

  trap.LogLine(tag, collector);

  TEST_ASSERT_EQUAL_STRING("   0:A{0x0D}{0x0A}B{0x09}{0x00}{0x7F}C\n",
                           stream.str().c_str());
}
}  // namespace ae::test_escaped_io_stream_trap

int test_escaped_io_stream_trap() {
  using namespace ae::test_escaped_io_stream_trap;  // NOLINT
  UNITY_BEGIN();
  RUN_TEST(test_ControlCharactersAreRenderedAsHex);
  return UNITY_END();
}
