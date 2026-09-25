/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_API_PROTOCOL_DETAILS_PACKET_LIST_H_
#define AETHER_API_PROTOCOL_DETAILS_PACKET_LIST_H_

#include <etl/alignment.h>
#include <etl/vector.h>

#include "aether/types/data_buffer.h"

#include "aether/api_protocol/details/api_message.h"
#include "aether/api_protocol/details/api_packer.h"

namespace ae {
class IPackMessage {
 public:
  virtual ~IPackMessage() = default;
  virtual void Pack(ApiPacker& packer) && = 0;
};

template <typename M>
class PackMessage;

template <std::uint8_t id, typename M>
class PackMessage<ApiMessage<id, M>> final : public IPackMessage {
 public:
  using Message = ApiMessage<id, M>;
  template <typename U>
    requires(std::same_as<std::decay_t<U>, Message>)
  explicit PackMessage(U&& message) : message_{std::forward<U>(message)} {}

  void Pack(ApiPacker& packer) && override {
    packer.Pack(Message::kMessageId, std::move(message_.message));
  }

 private:
  [[no_unique_address]] Message message_;
};

class IPacketList {
 public:
  /**
   * \brief Push new message to the packet list.
   */
  template <typename M>
  bool Push(M&& message) & {
    if (full()) {
      return false;
    }

    // add new message into the pull
    auto* elem = Allocate();
    assert(elem != nullptr);
    new (elem) PackMessage<std::decay_t<M>>(std::forward<M>(message));
    return true;
  }

  virtual DataBuffer Pack() && = 0;
  virtual bool full() const = 0;

 protected:
  IPacketList() = default;
  ~IPacketList() = default;

  virtual IPackMessage* Allocate() = 0;
};

/**
 * \brief Stack of packed messages to generate one packet
 */
template <std::size_t MaxSize, std::size_t MaxAlign, std::size_t Capacity>
class PacketList final : public IPacketList {
 public:
  PacketList() = default;

  PacketList(PacketList const&) = delete;
  PacketList& operator=(PacketList const&) = delete;
  PacketList(PacketList&&) = delete;
  PacketList& operator=(PacketList&&) = delete;
  ~PacketList() {
    for (auto const& e : list_) {
      std::destroy_at(e.template get_address<IPackMessage>());
    }
  }

  DataBuffer Pack() && override {
    DataBuffer buffer;
    ApiPacker packer{buffer};
    for (auto& p : list_) {
      auto& packet = p.template get_reference<IPackMessage>();
      std::move(packet).Pack(packer);
    }
    return buffer;
  }
  bool full() const override { return list_.full(); }

 protected:
  IPackMessage* Allocate() override {
    auto& ref = list_.emplace_back();
    return ref.template get_address<IPackMessage>();
  }

 private:
  using element_type = etl::aligned_storage_t<MaxSize, MaxAlign>;
  etl::vector<element_type, Capacity> list_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_PACKET_LIST_H_
