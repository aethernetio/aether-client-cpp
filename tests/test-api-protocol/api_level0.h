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

#ifndef TESTS_TEST_API_PROTOCOL_API_LEVEL0_H_
#define TESTS_TEST_API_PROTOCOL_API_LEVEL0_H_

#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <iostream>

#include "aether/api_protocol/api_protocol.h"
#include "aether/crc.h"

#include "api_level1.h"
#include "api_with_result.h"

namespace ae {
class ApiLevel0 : public ApiClass {
 public:
  static constexpr auto kClassId = 1;

  struct Message1 : public Message<Message1> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ApiLevel0::Message1");

    int i_data_;
    float f_data_;
    std::string s_data_;

    AE_CLASS_REFLECT(i_data_, f_data_, s_data_)
  };

  struct Message2 : public Message<Message2> {
    std::vector<std::uint8_t> child_messages_;

    AE_CLASS_REFLECT(child_messages_)
  };

  struct Message3 : public Message<Message3> {
    std::vector<std::uint8_t> child_messages_;

    AE_CLASS_REFLECT(child_messages_)
  };

  // for server
  void LoadFactory(MessageId code, ApiParser& parser) override {
    switch (code) {
      case 1:
        parser.Load<Message1>(*this);
        break;
      case 2:
        parser.Load<Message2>(*this);
        break;
      case 3:
        parser.Load<Message3>(*this);
        break;
      default:
        break;
    }
  }

  void Execute(Message1&& message, ApiParser& parser) {
    std::cout << "ApiLevel0::Execute(Message1)" << std::endl;
    std::cout << "i_data " << message.i_data_ << " f_data " << message.f_data_
              << " s_data " << message.s_data_ << std::endl;

    parser.Context().MessageNotify(std::move(message));
  }

  void Execute(Message2&& message, ApiParser& parser) {
    std::cout << "ApiLevel0::Execute(Message2)" << std::endl;
    // parse child api
    ApiLevel1 api_level1;
    auto chil_parser = ApiParser(parser.Context(), message.child_messages_);
    chil_parser.Parse(api_level1);
  }

  void Execute(Message3&& message, ApiParser& parser) {
    std::cout << "ApiLevel0::Execute(Message3)" << std::endl;
    // parse child api
    ApiWithResult api_with_result;
    auto chil_parser = ApiParser(parser.Context(), message.child_messages_);
    chil_parser.Parse(api_with_result);
  }

  // for client
  void Pack(Message1 message, ApiPacker& packer) { packer.Pack(1, message); }
  void Pack(Message2 message, ApiPacker& packer) { packer.Pack(2, message); }
  void Pack(Message3 message, ApiPacker& packer) { packer.Pack(3, message); }
};

}  // namespace ae

#endif  // TESTS_TEST_API_PROTOCOL_API_LEVEL0_H_ */
