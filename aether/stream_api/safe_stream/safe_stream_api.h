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

#ifndef AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
#define AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_

#include <cstdint>
#include <utility>

#include "aether/crc.h"
#include "aether/reflect/reflect.h"
#include "aether/transport/data_buffer.h"
#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae {
class SafeStreamApi : public ApiClass {
 public:
  static constexpr auto kClassId =
      crc32::checksum_from_literal("SafeStreamApi");

  struct Close : public Message<Close> {
    static constexpr auto kMessageCode = 2;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::Close");

    AE_REFLECT()
  };
  struct RequestReport : public Message<RequestReport> {
    static constexpr auto kMessageCode = 3;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::RequestReport");

    AE_REFLECT()
  };
  struct PutReport : public Message<PutReport> {
    static constexpr auto kMessageCode = 4;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::PutReport");

    AE_REFLECT_MEMBERS(offset)

    std::uint16_t offset;
  };
  struct Confirm : public Message<Confirm> {
    static constexpr auto kMessageCode = 5;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::Confirm");

    AE_REFLECT_MEMBERS(offset)

    std::uint16_t offset;
  };
  struct RequestRepeat : public Message<RequestRepeat> {
    static constexpr auto kMessageCode = 6;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::RequestRepeat");

    AE_REFLECT_MEMBERS(offset)

    std::uint16_t offset;
  };
  struct Send : public Message<Send> {
    static constexpr auto kMessageCode = 7;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::Send");

    AE_REFLECT_MEMBERS(offset, data)

    std::uint16_t offset;
    DataBuffer data;
  };
  struct Repeat : public Message<Repeat> {
    static constexpr auto kMessageCode = 8;
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("SafeStreamApi::Repeat");

    AE_REFLECT_MEMBERS(repeat_count, offset, data)

    std::uint16_t repeat_count;
    std::uint16_t offset;
    DataBuffer data;
  };

  void LoadFactory(MessageId message_id, ApiParser& parser) override;

  template <typename TMessage>
  void Execute(TMessage&& message, ApiParser& parser);

  template <typename TMessage>
  void Pack(TMessage&& message, ApiPacker& packer) {
    packer.Pack(TMessage::kMessageCode, std::forward<TMessage>(message));
  }
};
}  // namespace ae

#endif  // AETHER_STREAM_API_SAFE_STREAM_SAFE_STREAM_API_H_
