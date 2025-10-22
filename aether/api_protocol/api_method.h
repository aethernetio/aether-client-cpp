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

#include "aether/actions/action_context.h"
#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/api_pack_parser.h"
#include "aether/api_protocol/protocol_context.h"
#include "aether/api_protocol/api_promise_action.h"

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
struct Method<MessageCode, ApiPromisePtr<R>(Args...), ArgProc> {
  explicit Method(ProtocolContext& protocol_context,
                  ActionContext action_context, ArgProc arg_proc = {})
      : protocol_context_{&protocol_context},
        action_context_{std::move(action_context)},
        arg_proc_{std::move(arg_proc)} {}

  ApiPromisePtr<R> operator()(Args... args) {
    auto request_id = RequestId::GenRequestId();
    auto* packet_stack = protocol_context_->packet_stack();
    assert(packet_stack);
    packet_stack->Push(*this,
                       arg_proc_(request_id, std::forward<Args>(args)...));

    auto promise_ptr = ApiPromisePtr<R>{action_context_, request_id};

    protocol_context_->AddSendResultCallback(
        request_id, [p_ptr{promise_ptr}](ApiParser& parser) mutable {
          assert(p_ptr);
          if constexpr (!std::is_same_v<void, R>) {
            auto value = parser.Extract<R>();
            p_ptr->SetValue(value);
          } else {
            p_ptr->SetValue();
          }
        });
    protocol_context_->AddSendErrorCallback(
        request_id, [p_ptr{promise_ptr}](auto, auto) mutable {
          assert(p_ptr);
          p_ptr->Reject();
        });

    return promise_ptr;
  }

  template <typename... Ts>
  void Pack(GenericMessage<Ts...>&& message, ApiPacker& packer) {
    packer.Pack(MessageCode, std::move(message));
  }

 private:
  ProtocolContext* protocol_context_;
  ActionContext action_context_;
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
