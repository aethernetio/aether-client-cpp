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

#ifndef AETHER_API_PROTOCOL_CHILD_DATA_H_
#define AETHER_API_PROTOCOL_CHILD_DATA_H_

#include <vector>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/packet_builder.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
class ChildData {
 public:
  ChildData();

  ChildData(std::vector<std::uint8_t> data);
  ChildData(std::unique_ptr<IPackMessage> pack_message);

  template <typename TApiClass, typename... TMessages>
  ChildData(PackMessage<TApiClass, TMessages...> pack_message)
      : ChildData{std::make_unique<PackMessage<TApiClass, TMessages...>>(
            std::move(pack_message))} {}

  ChildData(PacketBuilder&& packet_builder)
      : ChildData{std::move(packet_builder).Pack()} {}

  ChildData(ChildData&& other) : pack_data_{std::move(other.pack_data_)} {}
  ChildData(ChildData const& other) : ChildData{other.PackData()} {}

  ChildData& operator=(ChildData const& other) {
    if (this != &other) {
      pack_data_ = other.PackData();
    }
    return *this;
  }

  ChildData& operator=(ChildData&& other) {
    if (this != &other) {
      pack_data_ = std::move(other.pack_data_);
    }
    return *this;
  }

  std::vector<std::uint8_t> PackData(ProtocolContext& protocol_context) &&;
  std::vector<std::uint8_t> const& PackData() const;

  void clear() { pack_data_ = std::vector<std::uint8_t>{}; }

 private:
  static std::vector<std::uint8_t> DataPackMessage(
      ProtocolContext& protocol_context, IPackMessage&& pack_message);

  std::variant<std::unique_ptr<IPackMessage>, std::vector<std::uint8_t>>
      pack_data_;
};

template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& is, ChildData& ch_d) {
  std::vector<std::uint8_t> data;
  is >> data;
  ch_d = ChildData{data};
  return is;
}

inline message_ostream& operator<<(message_ostream& os, ChildData const& ch_d) {
  os << std::move(const_cast<ChildData&>(ch_d))
            .PackData(os.ob_.packer.Context());
  return os;
}

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_CHILD_DATA_H_
