/*
 * Copyright 2024 Aethernet Inc.
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

#include "unity.h"

#include "aether/memory.h"
#include "aether/actions/action_processor.h"
#include "aether/actions/action_context.h"

#include "aether/stream_api/gates_stream.h"

#include "tests/test-stream/mock_write_stream.h"
#include "tests/test-stream/to_data_buffer.h"

namespace ae::tes_templated_streams {

class IntToBytesGate {
 public:
  DataBuffer WriteIn(int in_data) {
    return DataBuffer{
        reinterpret_cast<uint8_t const*>(&in_data),
        reinterpret_cast<uint8_t const*>(&in_data) + sizeof(in_data)};
  }

  int WriteOut(DataBuffer const& data) {
    int value = *reinterpret_cast<int const*>(data.data());
    return value;
  }
};

class StringIntGate {
 public:
  int WriteIn(std::string const& in_data) { return std::stoi(in_data); }
};

void test_IntToBytesGate() {
  ActionProcessor ap;
  ActionContext ac{ap};

  DataBuffer written_data;
  int read_data;

  auto stream = GatesStream{IntToBytesGate{}};

  auto write_gate = MockWriteStream{ac, 1000};

  Tie(stream, write_gate);

  write_gate.on_write_event().Subscribe(
      [&](auto data) { written_data = std::move(data); });

  stream.Write(10);

  stream.out_data_event().Subscribe(
      [&](auto const& data) { read_data = data; });

  TEST_ASSERT_EQUAL(sizeof(int), written_data.size());

  write_gate.WriteOut(written_data);

  TEST_ASSERT_EQUAL(10, read_data);
}

void test_StringIntGate() {
  ActionProcessor ap;
  ActionContext ac{ap};

  DataBuffer written_data;
  int read_data;

  auto write_gate = MockWriteStream{ac, static_cast<std::size_t>(1000)};
  auto _0 = write_gate.on_write_event().Subscribe(
      [&](auto data) { written_data = std::move(data); });

  auto stream = GatesStream{StringIntGate{}, IntToBytesGate{}};
  auto r = TiedWriteOut(ToDataBuffer("const T (&arr)[size]"), StringIntGate{},
                        IntToBytesGate{});
  // using gt = decltype(stream)::Base;
  // using os = decltype(stream)::OutStream;
  // using out = gt::TypeOut;
  // using in = gt::TypeIn;
  // using os_out = os::TypeOut;
  // using os_in = os::TypeIn;

  Tie(stream, write_gate);

  stream.out_data_event().Subscribe(
      [&](auto const& data) { read_data = data; });

  stream.Write("10");

  TEST_ASSERT_EQUAL(sizeof(int), written_data.size());

  write_gate.WriteOut(written_data);

  TEST_ASSERT_EQUAL(10, read_data);
}

}  // namespace ae::tes_templated_streams
int test_templated_streams() {
  UNITY_BEGIN();

  RUN_TEST(ae::tes_templated_streams::test_IntToBytesGate);
  RUN_TEST(ae::tes_templated_streams::test_StringIntGate);

  return UNITY_END();
}
