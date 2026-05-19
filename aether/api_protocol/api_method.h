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

#ifndef AETHER_API_PROTOCOL_API_METHOD_H_
#define AETHER_API_PROTOCOL_API_METHOD_H_

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/etl/include/etl/pool.h"
DISABLE_WARNING_POP()

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/api_promise.h"
#include "aether/api_protocol/api_pack_parser.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
class DefaultArgProc {
 public:
  template <typename... Args>
  auto operator()(Args&&... args) {
    return GenericMessage{std::forward<Args>(args)...};
  }
};

/**
 * \brief Method call implementation.
 * It's a client side for generating request packets.
 * Each Method provide operator() so it fells like a real method call.
 * Method call should be performed in ApiContext \see ApiContext.
 */
template <MessageId MessageCode, typename Signature,
          typename ArgProc = DefaultArgProc, typename Enable = void>
struct Method;

/**
 * \brief Specialization for method with plain arguments.
 * A GenericMessage generated directly from list of args.
 */
template <MessageId MessageCode, typename... Args, typename ArgProc>
struct Method<MessageCode, void(Args...), ArgProc> {
  explicit Method(ProtocolContext& protocol_context, ArgProc arg_proc = {})
      : protocol_context_{&protocol_context}, arg_proc_{std::move(arg_proc)} {}

  void operator()(Args... args) {
    auto* packet_stack = protocol_context_->packet_stack();
    assert(packet_stack);
    packet_stack->Push(*this, arg_proc_(std::forward<Args>(args)...));
  }

  template <typename... Ts>
  void Pack(GenericMessage<Ts...>&& message, ApiPacker& packer) {
    packer.Pack(MessageCode, std::move(message));
  }

 private:
  ProtocolContext* protocol_context_;
  ArgProc arg_proc_;
};

/**
 * \brief Specialization for method with return value.
 * A GenericMessage generated with request id and list of args.
 * return PromiseView<R> for waiting the result or error.
 */
template <MessageId MessageCode, typename R, typename... Args, typename ArgProc>
struct Method<MessageCode, ApiPromise<R>(Args...), ArgProc> {
  explicit Method(ProtocolContext& protocol_context, ArgProc arg_proc = {})
      : protocol_context_{&protocol_context}, arg_proc_{std::move(arg_proc)} {}

  ~Method() noexcept {
    for (auto i = api_promise_pool_.begin(); i != api_promise_pool_.end();
         ++i) {
      api_promise_pool_.destroy(&i.template get<ApiPromise<R>>());
    }
  }

  ApiPromise<R>& operator()(Args... args) {
    auto request_id = RequestId::GenRequestId();
    auto* packet_stack = protocol_context_->packet_stack();
    assert(packet_stack);
    packet_stack->Push(*this,
                       arg_proc_(request_id, std::forward<Args>(args)...));
    return UpdateRequestCb(request_id);
  }

  template <typename... Ts>
  void Pack(GenericMessage<Ts...>&& message, ApiPacker& packer) {
    packer.Pack(MessageCode, std::move(message));
  }

 private:
  ApiPromise<R>& UpdateRequestCb(RequestId request_id) {
    auto* api_promise = api_promise_pool_.create(request_id);
    assert(api_promise != nullptr);

    if constexpr (!std::is_same_v<void, R>) {
      protocol_context_->AddSendResultCallback(
          request_id, [this, api_promise]() {
            api_promise->SetValue(
                protocol_context_->parser()->template Extract<R>());

            api_promise_pool_.destroy(api_promise);
          });
    } else {
      protocol_context_->AddSendResultCallback(
          request_id, [this, api_promise]() {
            api_promise->SetValue();
            api_promise_pool_.destroy(api_promise);
          });
    }
    protocol_context_->AddSendErrorCallback(
        request_id, [this, api_promise](auto, std::uint32_t err) {
          api_promise->SetError(std::move(err));
          api_promise_pool_.destroy(api_promise);
        });

    return *api_promise;
  }

  ProtocolContext* protocol_context_;
  // TODO: configure pool size
  etl::pool<ApiPromise<R>, 10> api_promise_pool_;
  ArgProc arg_proc_;
};

/**
 * \brief Specialization for method call with sub api.
 * A GenericMessage generated directly from list of args and subapi method call.
 * returns SubContext<Api> with access to Api class.
 */
template <MessageId MessageCode, typename Api, typename... Args,
          typename ArgProc>
struct Method<MessageCode, SubContext<Api>(Args...), ArgProc> {
  explicit Method(ProtocolContext& protocol_context, Api& api,
                  ArgProc arg_proc = {})
      : protocol_context_{&protocol_context},
        api_{&api},
        arg_proc_{std::move(arg_proc)} {}

  SubContext<Api> operator()(Args... args) {
    auto child_stack = ChildPacketStack{};
    SubContext context{*api_, *child_stack};

    auto* packet_stack = protocol_context_->packet_stack();
    assert(packet_stack);
    packet_stack->Push(
        *this, arg_proc_(std::forward<Args>(args)..., std::move(child_stack)));
    return context;
  }

  template <typename... Ts>
  void Pack(GenericMessage<Ts...>&& message, ApiPacker& packer) {
    packer.Pack(MessageCode, std::move(message));
  }

 private:
  ProtocolContext* protocol_context_;
  Api* api_;
  ArgProc arg_proc_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_METHOD_H_
