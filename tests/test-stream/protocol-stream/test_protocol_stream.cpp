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
#include "aether/api_protocol/api_protocol.h"

#include "aether/api_protocol/packet_builder.h"
#include "aether/stream_api/protocol_stream.h"

#include "aether/transport/data_buffer.h"
#include "tests/test-stream/mock_write_gate.h"

namespace ae::test_protocol_stream {
class TestApiClass : public ApiClass {
 public:
  static constexpr auto kClassId = 12;

  struct Message1 : public Message<Message1> {
    static constexpr auto kMessageId = 42;

    template <typename T>
    void Serializator(T& s) {
      s & value_2byte & value_4byte;
    }

    std::uint16_t value_2byte;
    std::uint32_t value_4byte;
  };

  struct Message2 : public Message<Message2> {
    static constexpr auto kMessageId = 24;

    template <typename T>
    void Serializator(T& s) {
      s & dynamic_size_value;
    }
    std::string dynamic_size_value;
  };

  struct DataMessage : public Message<DataMessage> {
    static constexpr auto kMessageId = 25;

    template <typename T>
    void Serializator(T& s) {
      s & data;
    }

    DataBuffer data;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override {
    switch (message_id) {
      case 1:
        parser.Load<Message1>(*this);
        break;
      case 2:
        parser.Load<Message2>(*this);
        break;
      case 3:
        parser.Load<DataMessage>(*this);
      default:
        break;
    }
  }

  void Execute(Message1&& message, ApiParser& parser) {
    parser.Context().MessageNotify(std::move(message));
  }
  void Execute(Message2&& message, ApiParser& parser) {
    parser.Context().MessageNotify(std::move(message));
  }
  void Execute(DataMessage&& message, ApiParser& parser) {
    parser.Context().MessageNotify(std::move(message));
  }

  void Pack(Message1&& message, ApiPacker& packer) {
    packer.Pack(1, std::move(message));
  }

  void Pack(Message2&& message, ApiPacker& packer) {
    packer.Pack(2, std::move(message));
  }
  void Pack(DataMessage&& message, ApiPacker& packer) {
    packer.Pack(3, std::move(message));
  }
};

static constexpr char test_data[] =
    "Did you know? A programmer tried to store his age using a byte, but on "
    "his 256th birthday the variable overflowed and he became 0 years old.";

static constexpr char test_data2[] =
    "Did you know? A group of programmers is called a 'merge conflict' in "
    "their natural habitat.";

void test_ProtocolWriteStream() {
  auto ap = ActionProcessor{};
  auto pc = ProtocolContext{};

  auto written_data = DataBuffer{};
  auto received_child_data = DataBuffer{};

  auto write_stream = MockWriteGate{ap, std::size_t{1000}};

  auto _0 = write_stream.on_write_event().Subscribe([&](auto data, auto) {
    written_data = std::move(data);
    auto api_parser = ApiParser{pc, written_data};
    auto api = TestApiClass{};
    api_parser.Parse(api);
  });

  auto _1 = pc.OnMessage<TestApiClass::DataMessage>([&](auto const& message) {
    received_child_data = message.message().data;
  });

  auto protocol_stream =
      ProtocolWriteGate{pc, TestApiClass{}, TestApiClass::Message1{{}, 1, 2}};

  Tie(protocol_stream, write_stream);

  auto data_size = protocol_stream.stream_info().max_element_size;
  TEST_ASSERT_EQUAL(1 + 2 + 4, 1000 - data_size);

  protocol_stream.Write(
      PacketBuilder{
          pc, PackMessage{TestApiClass{},
                          TestApiClass::DataMessage{
                              {}, {test_data, test_data + sizeof(test_data)}}}},
      TimePoint::clock::now());

  TEST_ASSERT_EQUAL(1 + 2 + 4 + 1 + 1 + sizeof(test_data), written_data.size());
  TEST_ASSERT_EQUAL_STRING(test_data, received_child_data.data());

  written_data.clear();
  received_child_data.clear();

  auto str = std::string{"hello"};
  auto protocol_stream2 =
      ProtocolWriteGate{pc, TestApiClass{}, TestApiClass::Message2{{}, str}};

  Tie(protocol_stream2, write_stream);

  auto data_size2 = protocol_stream2.stream_info().max_element_size;
  TEST_ASSERT_EQUAL(1 + 1 + str.size(), 1000 - data_size2);

  protocol_stream2.Write(
      PacketBuilder{
          pc,
          PackMessage{TestApiClass{},
                      TestApiClass::DataMessage{
                          {}, {test_data2, test_data2 + sizeof(test_data2)}}}},
      TimePoint::clock::now());

  TEST_ASSERT_EQUAL(1 + 1 + str.size() + 1 + 1 + sizeof(test_data2),
                    written_data.size());
  TEST_ASSERT_EQUAL_STRING(test_data2, received_child_data.data());
}

void test_ProtocolReadStream() {
  auto ap = ActionProcessor{};
  auto pc = ProtocolContext{};

  auto msg1_received = false;
  auto msg2_received = false;

  auto _0 = pc.OnMessage<TestApiClass::Message1>(
      [&](auto const& /* message */) { msg1_received = true; });

  auto _1 = pc.OnMessage<TestApiClass::Message2>(
      [&](auto const& /* message */) { msg2_received = true; });

  auto write_stream = MockWriteGate{ap, std::size_t{1000}};

  auto protocol_stream = ProtocolReadGate{pc, TestApiClass{}};

  Tie(protocol_stream, write_stream);

  write_stream.WriteOut(PacketBuilder{
      pc,
      PackMessage{
          TestApiClass{},
          TestApiClass::Message1{{}, 1, 2},
      },
  });

  TEST_ASSERT(msg1_received);

  write_stream.WriteOut(PacketBuilder{
      pc,
      PackMessage{
          TestApiClass{},
          TestApiClass::Message2{{}, "hello"},
      },
  });

  TEST_ASSERT(msg2_received);
}

}  // namespace ae::test_protocol_stream

int test_protocol_stream() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_protocol_stream::test_ProtocolWriteStream);
  RUN_TEST(ae::test_protocol_stream::test_ProtocolReadStream);
  return UNITY_END();
}
