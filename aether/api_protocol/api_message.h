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

#ifndef AETHER_API_PROTOCOL_API_MESSAGE_H_
#define AETHER_API_PROTOCOL_API_MESSAGE_H_

#include <cstdint>
#include <vector>

#include "aether/packed_int.h"
#include "aether/mstream.h"
#include "aether/mstream_buffers.h"

namespace ae {

using MessageId = std::uint8_t;

using PackedSize = Packed<std::uint64_t, std::uint8_t, 250>;

class ApiParser;
class ApiPacker;

struct MessageBufferWriter : VectorWriter<PackedSize> {
  MessageBufferWriter(std::vector<uint8_t>& data, ApiPacker& p)
      : VectorWriter(data), packer{p} {}

  ApiPacker& packer;
};
struct MessageBufferReader : VectorReader<PackedSize> {
  MessageBufferReader(std::vector<uint8_t> const& data, ApiParser& p)
      : VectorReader(data), parser{p} {}
  ApiParser& parser;
};

using message_ostream = ae::omstream<MessageBufferWriter>;
using message_istream = ae::imstream<MessageBufferReader>;

// Base for all messages
template <typename T>
struct Message {
  Message() = default;
  Message(Message const&) = default;
  void Load(message_istream& ms) { ms >> static_cast<T&>(*this); }
  void Save(message_ostream& ms) const { ms << static_cast<T const&>(*this); }
};

/**
 * \brief A message formed from template parameters
 */
template <typename... Ts>
struct GenericMessage : Message<GenericMessage<Ts...>> {
  explicit GenericMessage() = default;
  explicit GenericMessage(Ts... args) : fields{std::forward<Ts>(args)...} {}

  template <typename Ib>
  friend imstream<Ib>& operator>>(imstream<Ib>& is, GenericMessage& mesassge) {
    std::apply([&](auto&... args) { ((is >> args), ...); }, mesassge.fields);
    return is;
  }

  template <typename Ob>
  friend omstream<Ob>& operator<<(omstream<Ob>& os,
                                  GenericMessage const& mesassge) {
    std::apply([&](auto const&... args) { ((os << args), ...); },
               mesassge.fields);
    return os;
  }

  std::tuple<Ts...> fields;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_MESSAGE_H_
