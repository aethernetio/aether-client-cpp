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

#ifndef AETHER_API_PROTOCOL_DETAILS_API_CONTEXT_H_
#define AETHER_API_PROTOCOL_DETAILS_API_CONTEXT_H_

#include <utility>

#include "aether/common.h"
#include "aether/config.h"

#include "aether/api_protocol/details/api_class_declare.h"
#include "aether/api_protocol/details/packet_list.h"

namespace ae {

static inline constexpr std::size_t kMaxApiCallPerApiContext =
    AE_API_PROTOCOL_MAX_API_CALL_PER_CONTEXT;

/**
 * \brief Api method call context.
 * Wrapper for api class. Pointer to TApi return through proxy object which
 * setting the context.
 * Methods called in one context form one packet.
 * The referenced API and ProtocolContext, when provided, must outlive this
 * context, its active call proxy, and every synchronous call or Pack().
 */
template <DeclaredApi Api>
class ApiContext {
 public:
  struct ClientApiContextImpl : public ClientApiContext {
   public:
    ClientApiContextImpl() = default;
    explicit ClientApiContextImpl(ProtocolContext& pc)
        : protocol_context_{&pc} {}

    IPacketList& packet_list() override { return packet_list_; }
    ProtocolContext& protocol_context() override {
      assert(protocol_context_ != nullptr &&
             "Protocol context requested but it's null");
      return *protocol_context_;
    }

    using PacketList =
        decltype(Api::template MakePacketList<kMaxApiCallPerApiContext>());
    PacketList packet_list_;
    ProtocolContext* protocol_context_{nullptr};
  };

  // Push and pop client api context for an API method call. The context and
  // referenced API must outlive the returned proxy.
  struct ApiCallProxy {
    ApiCallProxy(Api& api, ClientApiContextImpl& context_impl) : api_{&api} {
      api_->set_client_context(context_impl);
    }
    ~ApiCallProxy() { api_->reset_client_context(); }

    [[nodiscard]] Api* operator->() { return api_; }

   private:
    Api* api_;
  };

  /**
   * \brief Api context for simple method calls
   */
  explicit ApiContext(Api& api) : api_{&api}, context_impl_{} {}
  /**
   * \brief Api context with shared protocol context
   */
  ApiContext(Api& api, ProtocolContext& pc) : api_{&api}, context_impl_{pc} {}

  ApiContext(ApiContext const&) = delete;
  ApiContext& operator=(ApiContext const&) = delete;
  ApiContext(ApiContext&&) = delete;
  ApiContext& operator=(ApiContext&&) = delete;

  // return the object setting the context of the api method call
  [[nodiscard]] ApiCallProxy operator->() {
    return ApiCallProxy{*api_, context_impl_};
  }

  // Pack is terminal. Reusing or repacking this context is unsupported.
  [[nodiscard]] DataBuffer Pack() && {
    return std::move(context_impl_.packet_list_).Pack();
  }

  operator DataBuffer() &&  // NOLINT (*explicit*)
  {
    return std::move(*this).Pack();
  }

 private:
  Api* api_;
  ClientApiContextImpl context_impl_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_API_CONTEXT_H_
