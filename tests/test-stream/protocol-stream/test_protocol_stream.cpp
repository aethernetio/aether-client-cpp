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

#include <unity.h>

#include "aether/actions/action_context.h"
#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_class_impl.h"

#include "aether/stream_api/protocol_gates.h"
#include "aether/api_protocol/packet_builder.h"

#include "aether/transport/data_buffer.h"
#include "tests/test-stream/to_data_buffer.h"
#include "tests/test-stream/mock_write_stream.h"

namespace ae::test_protocol_stream {
class TestApiClass : public ApiClassImpl<TestApiClass> {
 public:
  explicit TestApiClass(ProtocolContext& protocol_context)
      : method1{protocol_context},
        method2{protocol_context},
        data_method{protocol_context} {}

  Method<42, void(std::uint16_t value_2byte, std::uint32_t value_4byte)>
      method1;
  Method<24, void(std::string dynamic_size_value)> method2;
  Method<25, void(DataBuffer data)> data_method;

  void Method1Impl(ApiParser& parser, std::uint16_t value_2byte,
                   std::uint32_t value_4byte) {
    method1_event.Emit(value_2byte, value_4byte);
  }
  void Method2Impl(ApiParser& parser, std::string dynamic_size_value) {
    method2_event.Emit(dynamic_size_value);
  }
  void DataMethodImpl(ApiParser& parser, DataBuffer data) {
    data_event.Emit(data);
  }

  using ApiMethods = ImplList<RegMethod<42, &TestApiClass::Method1Impl>,
                              RegMethod<24, &TestApiClass::Method2Impl>,
                              RegMethod<25, &TestApiClass::DataMethodImpl>>;

  Event<void(std::uint16_t value_2byte, std::uint32_t value_4byte)>
      method1_event;
  Event<void(std::string const& dynamic_size_value)> method2_event;
  Event<void(DataBuffer const& data)> data_event;
};

static constexpr char test_data[] =
    "Did you know? A programmer tried to store his age using a byte, but "
    "on "
    "his 256th birthday the variable overflowed and he became 0 years "
    "old.";

static constexpr char test_data2[] =
    "Did you know? A group of programmers is called a 'merge conflict' "
    "in "
    "their natural habitat.";

void test_ProtocolReadStream() {
  auto ap = ActionProcessor{};
  auto pc = ProtocolContext{};
  auto api = TestApiClass{pc};

  auto msg1_received = false;
  auto msg2_received = false;

  EventSubscriber{api.method1_event}.Subscribe(
      [&](auto, auto) { msg1_received = true; });

  EventSubscriber{api.method2_event}.Subscribe(
      [&](auto) { msg2_received = true; });

  auto protocol_gate = ProtocolReadGate{pc, api};

  auto api_context = ApiContext{pc, api};

  api_context->method1(1, 2);
  protocol_gate.WriteOut(std::move(api_context));

  TEST_ASSERT(msg1_received);

  api_context->method2("hello");
  protocol_gate.WriteOut(std::move(api_context));

  TEST_ASSERT(msg2_received);
}

void test_ApiClassAdapter() {
  auto ap = ActionProcessor{};
  auto pc = ProtocolContext{};
  auto api = TestApiClass{pc};

  auto written_data = DataBuffer{};
  auto received_child_data = DataBuffer{};

  auto write_stream = MockWriteStream{ap, std::size_t{1000}};

  write_stream.on_write_event().Subscribe([&](auto data) {
    written_data = std::move(data);
    auto api_parser = ApiParser{pc, written_data};
    api_parser.Parse(api);
  });

  EventSubscriber{api.data_event}.Subscribe(
      [&](auto const& data) { received_child_data = data; });
  EventSubscriber{api.method2_event}.Subscribe([&](auto const& data) {
    received_child_data = DataBuffer{
        reinterpret_cast<const std::uint8_t*>(data.data()),
        reinterpret_cast<const std::uint8_t*>(data.data()) + data.size()};
  });

  auto api_adapter = ApiCallAdapter{ApiContext{pc, api}, write_stream};

  api_adapter->data_method(ToDataBuffer(test_data));
  api_adapter.Flush();

  TEST_ASSERT_EQUAL(1 + 1 + sizeof(test_data), written_data.size());
  TEST_ASSERT_EQUAL_STRING(test_data, received_child_data.data());

  written_data.clear();
  received_child_data.clear();

  auto str = std::string{"hello"};
  api_adapter->method2(str);
  api_adapter.Flush();

  TEST_ASSERT_EQUAL(1 + 1 + str.size(), written_data.size());
  TEST_ASSERT_EQUAL(str.size(), received_child_data.size());
  TEST_ASSERT_EQUAL_STRING_LEN(str.c_str(), received_child_data.data(),
                               str.size());
}

}  // namespace ae::test_protocol_stream

int test_protocol_stream() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_protocol_stream::test_ProtocolReadStream);
  RUN_TEST(ae::test_protocol_stream::test_ApiClassAdapter);
  return UNITY_END();
}
