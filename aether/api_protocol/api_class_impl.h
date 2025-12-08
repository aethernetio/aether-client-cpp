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

#include "aether/reflect/reflect.h"
#include "aether/api_protocol/api_class.h"
#include "aether/api_protocol/api_pack_parser.h"

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
struct SubContextImpl {
  SubContextImpl(ProtocolContext& protocol_context,
                 std::vector<std::uint8_t>&& data)
      : sub_data{std::move(data)}, parser{protocol_context, sub_data} {}

  void Parse(Api& api) { parser.Parse(api); }

  std::vector<std::uint8_t> sub_data;
  ApiParser parser;
};

template <typename T>
struct IsSubContextImpl : std::false_type {};
template <typename T>
struct IsSubContextImpl<SubContextImpl<T>> : std::true_type {};

/**
 * \brief Helper struct to invoke method implementations with received message
 */
template <MessageId message_id, typename TApi, auto method_ptr,
          typename Enable = void>
struct MethodInvoke;

/**
 * \brief Specialization for empty args.
 */
template <MessageId message_id, typename TApi, void (TApi::*method_ptr)()>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<>;

  static void Invoke(TApi* obj, Message&& /* message */, ApiParser&) {
    (obj->*method_ptr)();
  }
};

/**
 * \brief Specialization for method with plain args.
 */
template <MessageId message_id, typename TApi, typename First, typename... Args,
          void (TApi::*method_ptr)(First, Args...)>
struct MethodInvoke<message_id, TApi, method_ptr,
                    // solve instantiation ambiguity
                    std::enable_if_t<!IsPromiseResult<First>::value &&
                                     !IsSubContextImpl<First>::value>> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<First, Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser&) {
    std::apply(
        [&](auto&& first, auto&&... args) {
          (obj->*method_ptr)(std::forward<First>(first),
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
          void (TApi::*method_ptr)(PromiseResult<R>, Args...)>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<PromiseResult<R>, Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser&) {
    std::apply(
        [&](PromiseResult<R> promise, auto&&... args) {
          (obj->*method_ptr)(promise, std::forward<Args>(args)...);
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
          void (TApi::*method_ptr)(SubContextImpl<TSubApi>, Args...)>
struct MethodInvoke<message_id, TApi, method_ptr> {
 public:
  static constexpr auto kMessageCode = message_id;
  using Message = GenericMessage<Args...>;

  static void Invoke(TApi* obj, Message&& message, ApiParser& parser) {
    std::apply(
        [&](auto&&... args) {
          auto data = parser.Extract<std::vector<std::uint8_t>>();

          (obj->*method_ptr)(
              SubContextImpl<TSubApi>{parser.Context(), std::move(data)},
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
          void (TApi::*method_ptr)(Args...)>
struct RegMethod<message_id, method_ptr> {
  using Method = MethodInvoke<message_id, TApi, method_ptr>;
};

/**
 * \brief A helper for registration api extension.
 */
template <auto p>
struct ExtApi;

template <typename T, typename M, M T::* ptr>
struct ExtApi<ptr> {
  using type = T;
  using Field = reflect::reflect_internal::FieldPtr<T, ptr>;
};

/**
 * \brief A registered list of method implementation.
 * Each Api class should provide ApiMethods alias to instance of ImplList.
 * Use RegMethod or ExtApi for each element.
 */
template <typename... TRegs>
struct ImplList {
  using Impls = TypeList<TRegs...>;
};

template <typename T, typename = void>
struct HasApiMethods : std::false_type {};

template <typename T>
struct HasApiMethods<T, std::void_t<decltype(T::ApiMethods())>>
    : std::true_type {};

template <typename Api, typename T>
struct LoadSelector;

template <typename ImplList, typename Api, std::size_t... Is>
static bool ApiLoadFactory(Api* api, MessageId message_id, ApiParser& parser,
                           std::index_sequence<Is...>) {
  return (
      LoadSelector<Api, TypeAtT<Is, ImplList>>::Load(api, message_id, parser) ||
      ...);
}

template <typename Api, MessageId Id, auto method>
struct LoadSelector<Api, RegMethod<Id, method>> {
  static bool Load(Api* api, MessageId message_id, ApiParser& parser) {
    if (Id != message_id) {
      return false;
    }
    using Method = typename RegMethod<Id, method>::Method;
    auto message = parser.Extract<typename Method::Message>();
    Method::Invoke(api, std::move(message), parser);
    return true;
  }
};

template <typename Api, auto ptr>
struct LoadSelector<Api, ExtApi<ptr>> {
  static bool Load(Api* api, MessageId message_id, ApiParser& parser) {
    using Field = typename ExtApi<ptr>::Field;
    auto&& ext_api = Field::get(*api);
    return LoadFactoryImpl(&ext_api, message_id, parser);
  }
};

template <typename Api>
static bool LoadFactoryImpl(Api* api, MessageId message_id, ApiParser& parser) {
  static_assert(HasApiMethods<Api>::value, "Api should provide ApiMethods");
  using List = decltype(Api::ApiMethods());
  constexpr auto list_size = TypeListSize<typename List::Impls>;

  return ApiLoadFactory<typename List::Impls>(
      api, message_id, parser, std::make_index_sequence<list_size>());
}

/**
 * \brief Api Class Implementation.
 * Implements Api class with LoadFactory in generic way configured with Api.
 * Each api class should provide a list of implemented methods through
 * ApiMethods getter method returns \see ImplList.
 * Method invoke performed by pointer to method and *this* casted to selected
 * api class. Also Api class could provide api extensions through \see ExtList.
 */
template <typename Api>
class ApiClassImpl : public ApiClass {
 public:
  using ApiClass::ApiClass;

  void LoadFactory(MessageId message_id, ApiParser& parser) {
    auto res = LoadFactoryImpl(static_cast<Api*>(this), message_id, parser);
    if (!res) {
      assert(res);
    }
  }
};
}  // namespace ae

#define AE_METHODS(...) \
  static constexpr auto ApiMethods() { return ::ae::ImplList<__VA_ARGS__>{}; }

#endif  // AETHER_API_PROTOCOL_API_CLASS_IMPL_H_
