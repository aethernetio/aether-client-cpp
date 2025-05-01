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

#ifndef AETHER_API_PROTOCOL_API_CLASS_IMPL_H_
#define AETHER_API_PROTOCOL_API_CLASS_IMPL_H_

#include "aether/api_protocol/api_method.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae {
/**
 * \brief Use as tag for method with return value.
 */
template <typename R>
struct PromiseResult {
  AE_REFLECT_MEMBERS(request_id);
  RequestId request_id;
};

template <typename T>
struct IsPromiseResult : std::false_type {};
template <typename T>
struct IsPromiseResult<PromiseResult<T>> : std::true_type {};

/**
 * \brief Use as helper to continue parsing with subapi.
 */
template <typename Api>
struct SubApi {
  SubApi(ProtocolContext& protocol_context, std::vector<std::uint8_t>&& data)
      : sub_data{std::move(data)}, parser{protocol_context, sub_data} {}

  void Parse(Api& api) { parser.Parse(api); }

  std::vector<std::uint8_t> sub_data;
  ApiParser parser;
};

template <typename T>
struct IsSubApi : std::false_type {};
template <typename T>
struct IsSubApi<SubApi<T>> : std::true_type {};

/**
 * \brief Helper struct to invoke method implementations with received message
 */
template <MessageId message_id, typename TApi, auto method_ptr,
          typename Enable = void>
struct MethodInvoke;

/**
 * \brief Specialization for empty args.
 */
template <MessageId message_id, typename TApi,
          void (TApi::*method_ptr)(ApiParser& parser)>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<>;

  static void Invoke(TApi* obj, Message&& /* message */, ApiParser& parser) {
    (obj->*method_ptr)(parser);
  }
};

/**
 * \brief Specialization for method with plain args.
 */
template <MessageId message_id, typename TApi, typename First, typename... Args,
          void (TApi::*method_ptr)(ApiParser& parser, First, Args...)>
struct MethodInvoke<message_id, TApi, method_ptr,
                    // solve instantiation ambiguity
                    std::enable_if_t<!IsPromiseResult<First>::value &&
                                     !IsSubApi<First>::value>> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<First, Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser& parser) {
    std::apply(
        [&](auto&& first, auto&&... args) {
          (obj->*method_ptr)(parser, std::forward<First>(first),
                             std::forward<Args>(args)...);
        },
        std::move(message).fields);
  }
};

/**
 * \brief Specialization for method with return value.
 * RequestId passed through PromiseResult<R>, R is a type should be returned.
 */
template <MessageId message_id, typename TApi, typename R, typename... Args,
          void (TApi::*method_ptr)(ApiParser& parser, PromiseResult<R>,
                                   Args...)>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<PromiseResult<R>, Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser& parser) {
    std::apply(
        [&](PromiseResult<R> promise, auto&&... args) {
          (obj->*method_ptr)(parser, promise, std::forward<Args>(args)...);
        },
        std::move(message).fields);
  }
};

/**
 * \brief Specialization for method with subapi.
 * SubApi<TSubApi> used for continue to parse subapi.
 */
template <MessageId message_id, typename TApi, typename TSubApi,
          typename... Args,
          void (TApi::*method_ptr)(ApiParser& parser, SubApi<TSubApi>, Args...)>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser& parser) {
    std::apply(
        [&](auto&&... args) {
          auto data = parser.Extract<std::vector<std::uint8_t>>();

          (obj->*method_ptr)(parser,
                             SubApi<TSubApi>{parser.Context(), std::move(data)},
                             std::forward<Args>(args)...);
        },
        std::move(message).fields);
  }
};

/**
 * \brief A helper for registration method implementation in ApiClassImpl.
 */
template <MessageId message_id, auto method>
struct RegMethod;

template <MessageId message_id, typename TApi, typename... Args,
          void (TApi::*method_ptr)(ApiParser& parser, Args...)>
struct RegMethod<message_id, method_ptr> {
  using Method = MethodInvoke<message_id, TApi, method_ptr>;
};

/**
 * \brief A registered list of method implementation.
 * Each Api class should provide ApiMethods alias to instance of ImplList.
 * Use RegMethod for each element.
 */
template <typename... TRegMethod>
struct ImplList {
  using Methods = std::tuple<typename TRegMethod::Method...>;
};

/**
 * \brief Api Class Implementation.
 * Implements ApiClass in generic way configured with MainApi and optional
 * ExtApis which extends main api class.
 * Each api class should provide a list of implemented methods through
 * ApiMethods alias \see ImplList.
 * MainApi should be derived from ExtApis.
 * Method invoke performed by pointer to method and *this* casted to selected
 * api class.
 */
template <typename MainApi, typename... ExtApis>
class ApiClassImpl : public ApiClass {
 public:
  void LoadFactory(MessageId message_id, ApiParser& parser) override {
    auto res = ApiLoadFactory<MainApi>(message_id, parser) ||
               (ApiLoadFactory<ExtApis>(message_id, parser) || ...);
    if (!res) {
      assert(false);
    }
  }  // namespace ae

 private:
  template <typename Api>
  bool ApiLoadFactory(MessageId message_id, ApiParser& parser) {
    constexpr auto list_size =
        std::tuple_size<typename Api::ApiMethods::Methods>();
    return ApiLoadFactoryImpl<Api>(message_id, parser,
                                   std::make_index_sequence<list_size>());
  }

  template <typename Api, std::size_t... Is>
  bool ApiLoadFactoryImpl([[maybe_unused]] MessageId message_id,
                          [[maybe_unused]] ApiParser& parser,
                          std::index_sequence<Is...> const&) {
    return ([&]() {
      using MethodImpl =
          std::tuple_element_t<Is, typename Api::ApiMethods::Methods>;
      if (MethodImpl::kMessageCode != message_id) {
        return false;
      }
      auto* self = static_cast<Api*>(static_cast<MainApi*>(this));
      auto message = parser.Extract<typename MethodImpl::Message>();
      MethodImpl::Invoke(self, std::move(message), parser);
      return true;
    }() || ...);
  }
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_CLASS_IMPL_H_
