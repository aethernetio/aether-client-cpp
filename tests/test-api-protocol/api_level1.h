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

#ifndef TESTS_TEST_API_PROTOCOL_API_LEVEL1_H_
#define TESTS_TEST_API_PROTOCOL_API_LEVEL1_H_

#include <array>
#include <utility>
#include <cstdint>
#include <iostream>

#include "aether/api_protocol/api_protocol.h"
#include "aether/crc.h"

namespace ae {
class ApiLevel1 : public ApiClass {
 public:
  static constexpr auto kClassId = 2;

  struct Message1 : public Message<Message1> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ApiLevel1::Message1");

    int i_data_;
    std::array<std::uint8_t, 32> arr_data_;

    AE_REFLECT_MEMBERS(i_data_, arr_data_)
  };

  struct Message2 : public Message<Message2> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("ApiLevel1::Message2");
    std::uint32_t u_data_;

    AE_REFLECT_MEMBERS(u_data_)
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
      default:
        break;
    }
  }

  void Execute(Message1 message, ApiParser& parser) {
    std::cout << "ApiLevel1::Execute(Message1)" << std::endl;
    std::cout << "i_data_ " << message.i_data_ << " arr_data_size "
              << message.arr_data_.size() << std::endl;

    parser.Context().MessageNotify(std::move(message));
  }

  void Execute(Message2 message, ApiParser& parser) {
    std::cout << "ApiLevel1::Execute(Message2)" << std::endl;
    std::cout << "u_data_ " << message.u_data_ << std::endl;

    parser.Context().MessageNotify(std::move(message));
  }

  // for client
  void Pack(Message1 message, ApiPacker& packer) { packer.Pack(1, message); }
  void Pack(Message2 message, ApiPacker& packer) { packer.Pack(2, message); }
};
}  // namespace ae

#endif  // TESTS_TEST_API_PROTOCOL_API_LEVEL1_H_ */
