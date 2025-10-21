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

#ifndef AETHER_API_PROTOCOL_PACKET_BUILDER_H_
#define AETHER_API_PROTOCOL_PACKET_BUILDER_H_

#include <tuple>
#include <utility>
#include <memory>
#include <cstdint>
#include <vector>

#include "aether/api_protocol/api_pack_parser.h"

namespace ae {

class IPackMessage {
 public:
  virtual ~IPackMessage() = default;

  virtual void Pack(ApiPacker& packer) && = 0;
};

template <typename TApiClass, typename... TApiMessages>
class PackMessage : public IPackMessage {
 public:
  template <typename UApiClass, typename... UApiMessages>
  explicit PackMessage(UApiClass&& api_class, UApiMessages&&... api_messages)
      : api_class_{
            std::forward<UApiClass>(api_class),
        }, api_messages_{std::forward<UApiMessages>(api_messages)...} {}

  void Pack(ApiPacker& packer) && override {
    std::apply(
        [this, packer{&packer}](auto&&... messages) {
          (api_class_.Pack(std::forward<decltype(messages)>(messages), *packer),
           ...);
        },
        std::move(api_messages_));
  }

 private:
  TApiClass api_class_;
  std::tuple<TApiMessages...> api_messages_;
};

template <typename TApiClass, typename... TApiMessages>
PackMessage(TApiClass&& api_class, TApiMessages&&... api_messages)
    -> PackMessage<TApiClass, std::decay_t<TApiMessages>...>;

class PacketBuilder {
 public:
  template <typename... TPackMessages>
  explicit PacketBuilder(ProtocolContext& protocol_context,
                         TPackMessages&&... pack_messages)
      : protocol_context_{protocol_context} {
    (pack_messages_.push_back(std::make_unique<std::decay_t<TPackMessages>>(
         std::forward<TPackMessages>(pack_messages))),
     ...);
  }

  template <typename TApiClass, typename... TApiMessages>
  void Push(TApiClass&& api_class, TApiMessages&&... api_messages) {
    Push(PackMessage{std::forward<TApiClass>(api_class),
                     std::forward<TApiMessages>(api_messages)...});
  }

  std::vector<std::uint8_t> Pack() && {
    auto data = std::vector<std::uint8_t>{};

    ApiPacker packer{protocol_context_, data};

    for (auto& pack_message : pack_messages_) {
      std::move(*pack_message).Pack(packer);
    }
    return data;
  }

  operator std::vector<std::uint8_t>() && { return std::move(*this).Pack(); }

 private:
  template <typename TPackMessage>
  void Push(TPackMessage&& pack_message) {
    pack_messages_.push_back(std::make_unique<std::decay_t<TPackMessage>>(
        std::forward<TPackMessage>(pack_message)));
  }

  ProtocolContext& protocol_context_;
  std::vector<std::unique_ptr<IPackMessage>> pack_messages_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_PACKET_BUILDER_H_
