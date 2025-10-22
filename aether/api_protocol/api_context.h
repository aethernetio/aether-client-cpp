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

#ifndef AETHER_API_PROTOCOL_API_CONTEXT_H_
#define AETHER_API_PROTOCOL_API_CONTEXT_H_

#include <vector>
#include <utility>

#include "aether/memory.h"
#include "aether/common.h"
#include "aether/api_protocol/packet_builder.h"
#include "aether/api_protocol/api_pack_parser.h"

namespace ae {
/**
 * \brief Stack of packed messages to generate one packet
 */
class PacketStack {
 public:
  PacketStack() = default;

  AE_CLASS_MOVE_ONLY(PacketStack)

  template <typename TApi, typename TMessage>
  void Push(TApi&& api, TMessage&& message) & {
    packets_.emplace_back(make_unique<PackMessage>(
        std::forward<TApi>(api), std::forward<TMessage>(message)));
  }

  void Pack(ApiPacker& packer) && {
    for (auto& packet : packets_) {
      std::move(*packet).Pack(packer);
    }
    packets_.clear();
  }

 private:
  std::vector<std::unique_ptr<IPackMessage>> packets_;
};

/**
 * \brief Used as a child data for sub api packet.
 * Provide it's own specialization for operator<< to message_ostream.
 */
class ChildPacketStack {
 public:
  ChildPacketStack() : packet_stack_{make_unique<PacketStack>()} {}

  AE_CLASS_MOVE_ONLY(ChildPacketStack)

  PacketStack& operator*() { return *packet_stack_; }

  friend message_ostream& operator<<(
      message_ostream& os, ChildPacketStack const& child_packet_stack) {
    auto data = std::vector<std::uint8_t>{};
    auto packer = ApiPacker{os.ob_.packer.Context(), data};
    std::move(*child_packet_stack.packet_stack_).Pack(packer);
    os << data;
    return os;
  }

 private:
  std::unique_ptr<PacketStack> packet_stack_;
};

/**
 * \brief Api method call context.
 * Wrapper for api class. Pointer to TApi return through proxy object which
 * setting the context.
 * Methods called in one context form one packet.
 */
template <typename TApi>
class ApiContext {
  template <MessageId MessageCode, typename Signature, typename ArgProc,
            typename Enable>
  friend struct Method;

 public:
  // push and pop packet stack for api method call
  struct ApiCallProxy {
    ApiCallProxy(TApi& api, PacketStack& packet_stack) : api_{&api} {
      api_->protocol_context().PushPacketStack(packet_stack);
    }
    ~ApiCallProxy() { api_->protocol_context().PopPacketStack(); }

    [[nodiscard]] TApi* operator->() { return api_; }

   private:
    TApi* api_;
  };

  explicit ApiContext(TApi& api) : api_{&api} {}

  AE_CLASS_MOVE_ONLY(ApiContext)

  // return the object setting the context of the api method call
  [[nodiscard]] ApiCallProxy operator->() {
    return ApiCallProxy{*api_, packet_stack_};
  }

  [[nodiscard]] std::vector<std::uint8_t> Pack() && {
    auto data = std::vector<std::uint8_t>{};

    ApiPacker packer{api_->protocol_context(), data};

    std::move(packet_stack_).Pack(packer);
    return data;
  }

  operator std::vector<std::uint8_t>() && { return std::move(*this).Pack(); }

 private:
  TApi* api_;
  PacketStack packet_stack_;
};

/**
 * \brief Sub API method call context.
 * Used for form sub child data packet from sub api method calls.
 */
template <typename TApi>
class SubContext {
  template <MessageId MessageCode, typename Signature, typename ArgProc,
            typename Enable>
  friend struct Method;

 public:
  SubContext(TApi& api, PacketStack& packet_stack)
      : api_{&api}, packet_stack_{&packet_stack} {}

  AE_CLASS_MOVE_ONLY(SubContext)

  // return the object setting the context of the api method call
  [[nodiscard]] typename ApiContext<TApi>::ApiCallProxy operator->() {
    return typename ApiContext<TApi>::ApiCallProxy{*api_, *packet_stack_};
  }

 private:
  TApi* api_;
  PacketStack* packet_stack_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_CONTEXT_H_
