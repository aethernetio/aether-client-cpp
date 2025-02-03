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
#include <cstdint>

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/protocol_context.h"
#include "aether/api_protocol/packet_builder.h"

#include "aether/api_protocol/send_result.h"
#include "aether/transport/low_level/tcp/data_packet_collector.h"
#include "api_level0.h"
#include "api_level1.h"
#include "assert_packet.h"
#include "tests/test-api-protocol/api_with_result.h"

void setUp() {}
void tearDown() {}

namespace ae::test_api_protocol {
void test_ApiLevel0_Message_1() {
  ProtocolContext p_context{};
  std::vector<std::uint8_t> pack_data;

  // client side
  {
    ApiLevel0 root;
    pack_data = PacketBuilder{
        p_context,
        PackMessage{
            root,
            ApiLevel0::Message1{
                {},
                12,
                42.f,
                "hello",
            },
        },
    };

    AssertPacket(pack_data, MessageId{1}, int{12}, float{42.f},
                 std::string{"hello"});
  }

  // server side
  {
    bool message_received = false;
    auto _ = p_context.MessageEvent<ApiLevel0::Message1>().Subscribe(
        [&message_received](auto const&) { message_received = true; });

    ApiParser parser{p_context, pack_data};
    ApiLevel0 root;
    parser.Parse(root);

    TEST_ASSERT(message_received);
  }
}

void test_ApiLevel1() {
  ProtocolContext p_context{};
  std::vector<std::uint8_t> pack_data;

  // client side
  {
    ApiLevel0 root;
    pack_data = PacketBuilder{
        p_context,
        PackMessage{
            root,
            ApiLevel0::Message2{
                {},
                PacketBuilder{
                    p_context,
                    PackMessage{
                        ApiLevel1{},
                        ApiLevel1::Message1{{}, 12, {1, 2, 3, 4}},
                        ApiLevel1::Message2{{}, 54},
                    },
                },
            },
        },
    };

    AssertPacket(pack_data, MessageId{2}, Skip<PacketSize>{}, MessageId{1},
                 int{12}, Skip<std::array<std::uint8_t, 32>>(), MessageId{2},
                 std::uint32_t{54});
  }
  // server side
  {
    bool message1_received = false;
    bool message2_received = false;
    auto _0 = p_context.MessageEvent<ApiLevel1::Message1>().Subscribe(
        [&message1_received](auto const& msg) { message1_received = true; });
    auto _1 = p_context.MessageEvent<ApiLevel1::Message2>().Subscribe(
        [&message2_received](auto const& msg) { message2_received = true; });

    ApiParser parser{p_context, pack_data};
    ApiLevel0 root;
    parser.Parse(root);

    TEST_ASSERT(message1_received);
    TEST_ASSERT(message2_received);
  }
}

void test_ApiWithResult() {
  ProtocolContext p_context{};
  std::vector<std::uint8_t> pack_data_request;
  std::vector<std::uint8_t> pack_data_response;

  RequestId req_id{12};

  // client side
  {
    pack_data_request = PacketBuilder{
        p_context,
        PackMessage{
            ApiLevel0{},
            ApiLevel0::Message3{
                {},
                PacketBuilder{
                    p_context,
                    PackMessage{
                        ApiWithResult{},
                        ApiWithResult::RequestEcho{
                            {}, req_id, "hello from the other side!"},
                    },
                },
            },
        },
    };

    AssertPacket(pack_data_request, MessageId{3}, Skip<PackedSize>{},
                 MessageId{2}, req_id,
                 std::string{"hello from the other side!"});
  }

  // server side
  {
    bool request_echo_received = false;

    auto _ = p_context.MessageEvent<ApiWithResult::RequestEcho>().Subscribe(
        [&request_echo_received, &p_context,
         &pack_data_response](auto const& action) {
          request_echo_received = true;
          pack_data_response = PacketBuilder{
              p_context,
              PackMessage{
                  ApiLevel0{},
                  ApiLevel0::Message3{
                      {},
                      PacketBuilder{
                          p_context,
                          PackMessage{
                              ApiWithResult{},
                              ApiWithResult::ResponseEcho{
                                  {},
                                  action.message().id_,
                                  action.message().message_},
                          },
                      },
                  },
              },
          };
        });

    std::unique_ptr<IPackMessage> pm = std::make_unique<
        PackMessage<ApiWithResult, ApiWithResult::ResponseEcho>>(PackMessage{
        ApiWithResult{}, ApiWithResult::ResponseEcho{{}, 12, "12"}});

    ApiParser parser{p_context, pack_data_request};
    ApiLevel0 root;
    parser.Parse(root);

    TEST_ASSERT(request_echo_received);

    AssertPacket(pack_data_response, MessageId{3}, Skip<PackedSize>{},
                 MessageId{0}, req_id,
                 std::string{"hello from the other side!"});
  }

  // client side 2
  {
    bool response_echo_received = false;

    auto _ = p_context.MessageEvent<ApiWithResult::ResponseEcho>().Subscribe(
        [&response_echo_received, &req_id](auto const& action) {
          response_echo_received = true;
          TEST_ASSERT(action.message().id_ == req_id);
          TEST_ASSERT_EQUAL_STRING("hello from the other side!",
                                   action.message().message_.c_str());
        });

    ApiParser parser{p_context, pack_data_response};
    ApiLevel0 root;
    parser.Parse(root);

    TEST_ASSERT(response_echo_received);
  }
}
}  // namespace ae::test_api_protocol

int main() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_api_protocol::test_ApiLevel0_Message_1);
  RUN_TEST(ae::test_api_protocol::test_ApiLevel1);
  RUN_TEST(ae::test_api_protocol::test_ApiWithResult);
  return UNITY_END();
}
