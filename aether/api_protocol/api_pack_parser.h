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

#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
class ApiParser;
class ApiPacker;

// Parsing raw data buffer to API messages
class ApiParser {
 public:
  ApiParser(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t> const& data);
  ~ApiParser();

  template <typename TApiClass>
  void Parse(TApiClass& api_class) {
    while (archive.buffer().read_offset < archive.buffer().buff.size()) {
      MessageId message_id{std::numeric_limits<MessageId>::max()};
      if (auto res = archive.Load(message_id); res.IsErr()) {
        // message_id didn't loaded
        assert(false && "message_id didn't loaded");
        return;
      }
      api_class.LoadFactory(message_id, *this);
    }
  }

  template <typename Message, typename TApiClass>
  void Load(TApiClass& api_class) {
    Message msg{};
    if (auto res = archive.Load(msg); res.IsErr()) {
      // message_id didn't loaded
      assert(false && "Message didn't loaded");
      return;
    }
    api_class.Execute(std::move(msg), *this);
  }

  template <typename T>
  T Extract() {
    T result{};
    if (auto res = archive.Load(result); res.IsErr()) {
      // message_id didn't loaded
      assert(false && "result didn't extracted");
    }
    return result;
  }

  // cancel parsing
  void Cancel();
  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  seri::BinaryArchive<MessageBuffer> archive;
};

// Packing API messages to raw data buffer
class ApiPacker {
 public:
  ApiPacker(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t>& data);
  ~ApiPacker();

  template <typename Message>
  void Pack(MessageId message_id, Message const& msg) {
    auto res = archive.Save(message_id);
    if (!res) {
      assert(false && "Message id didn't saved");
      return;
    }
    res = archive.Save(msg);
    if (!res) {
      assert(false && "Message didn't saved");
      return;
    }
  }

  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  seri::BinaryArchive<MessageBuffer> archive;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_PACK_PARSER_H_
