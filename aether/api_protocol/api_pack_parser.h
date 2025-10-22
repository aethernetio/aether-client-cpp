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

#ifndef AETHER_API_PROTOCOL_API_PACK_PARSER_H_
#define AETHER_API_PROTOCOL_API_PACK_PARSER_H_

#include <vector>
#include <limits>
#include <utility>
#include <cassert>
#include <cstdint>

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
class ChildData;

class ApiParser;
class ApiPacker;

// Parsing raw data buffer to API messages
class ApiParser {
 public:
  ApiParser(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t> const& data);
  ApiParser(ProtocolContext& protocol_context_, ChildData const& child_data);
  ~ApiParser();

  template <typename TApiClass>
  void Parse(TApiClass& api_class) {
    while (buffer_reader_.offset_ < buffer_reader_.data_.size()) {
      MessageId message_id{std::numeric_limits<MessageId>::max()};
      istream_ >> message_id;
      api_class.LoadFactory(message_id, *this);
    }
  }

  template <typename Message, typename TApiClass>
  void Load(TApiClass& api_class) {
    Message msg{};
    msg.Load(istream_);
    api_class.Execute(std::move(msg), *this);
  }

  template <typename T>
  T Extract() {
    T result{};
    istream_ >> result;
    return result;
  }

  // cancel parsing
  void Cancel();
  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  MessageBufferReader buffer_reader_;
  message_istream istream_{buffer_reader_};
};

// Packing API messages to raw data buffer
class ApiPacker {
 public:
  ApiPacker(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t>& data);
  ~ApiPacker();

  template <typename Message>
  void Pack(MessageId message_id, Message&& msg) {
    ostream_ << message_id;
    std::forward<Message>(msg).Save(ostream_);
  }

  MessageBufferWriter& Buffer();

  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  MessageBufferWriter buffer_writer_;
  message_ostream ostream_{buffer_writer_};
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_PACK_PARSER_H_
