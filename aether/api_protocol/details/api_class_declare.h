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

#ifndef AETHER_API_PROTOCOL_DETAILS_API_CLASS_DECLARE_H_
#define AETHER_API_PROTOCOL_DETAILS_API_CLASS_DECLARE_H_

#include <cassert>
#include <type_traits>
#include <utility>

#include "aether-miscpp/meta/type_list.h"

#include "aether/types/data_buffer.h"

#include "aether/api_protocol/details/api_internal_context.h"
#include "aether/api_protocol/details/api_message.h"
#include "aether/api_protocol/details/api_parser.h"
#include "aether/api_protocol/details/api_promise.h"
#include "aether/api_protocol/details/packet_list.h"
#include "aether/api_protocol/details/request_id.h"
#include "aether/api_protocol/details/return_result_api.h"

namespace ae {
template <typename Api>
class SubApi;

namespace api_class_declate_internal {
template <auto method1, auto method2>
struct AreMethodsSame : std::false_type {};
template <auto method>
struct AreMethodsSame<method, method> : std::true_type {};

template <auto m1, auto m2>
static constexpr inline auto AreMethodsSame_v = AreMethodsSame<m1, m2>::value;

struct NullRegMethod {};

template <MessageId message_id, auto method>
struct RegMethod {
  static constexpr auto kMessageId = message_id;
  static constexpr auto kMethod = method;
};

template <typename T>
struct IsRegMethod : std::false_type {};

template <MessageId message_id, auto method>
struct IsRegMethod<RegMethod<message_id, method>> : std::true_type {};

template <typename RegMethods>
struct ApiMessageIdsValid;

template <>
struct ApiMessageIdsValid<TypeList<>> : std::true_type {};

template <typename RegMethod, typename... Rest>
struct ApiMessageIdsValid<TypeList<RegMethod, Rest...>>
    : std::bool_constant<
          ((RegMethod::kMessageId != Rest::kMessageId) && ...) &&
          (RegMethod::kMessageId != ReturnResultApi::kSendResult) &&
          (RegMethod::kMessageId != ReturnResultApi::kSendError) &&
          ApiMessageIdsValid<TypeList<Rest...>>::value> {};

template <typename... RegMethods>
consteval bool AreApiMessageIdsValid(TypeList<RegMethods...>) {
  return ApiMessageIdsValid<TypeList<RegMethods...>>::value;
}

/**
 * Replace actual argument type to types usable for generating a message.
 * const refs a rvalues replace to plain type.
 * SubApi calls replaces to DataBuffer.
 */
template <typename T>
struct ArgsReplace {
  using type = T;
};

template <typename T>
struct ArgsReplace<T const&> : ArgsReplace<T> {};

template <typename T>
struct ArgsReplace<T&&> : ArgsReplace<T> {};

template <typename T>
struct ArgsReplace<SubApi<T>> {
  using type = DataBuffer;
};

template <auto method>
struct MessageTrait;

template <typename C, typename... Args, void (C::*method_prt)(Args...)>
struct MessageTrait<method_prt> {
  using args_list = TypeList<typename ArgsReplace<Args>::type...>;
  using message_type = GenericMessage<typename ArgsReplace<Args>::type...>;
  static constexpr bool kReturnValue = false;
};

template <typename C, typename Ret, typename... Args,
          ApiPromise<Ret> (C::*method_prt)(Args...)>
struct MessageTrait<method_prt> {
  using args_list = TypeList<typename ArgsReplace<Args>::type...>;
  using message_type =
      GenericMessage<RequestId, typename ArgsReplace<Args>::type...>;
  using return_type = Ret;
  static constexpr bool kReturnValue = true;
};

template <typename Reg>
  requires(IsRegMethod<Reg>::value)
struct ApiMessageTrait {
  using message_type =
      ApiMessage<Reg::kMessageId,
                 typename MessageTrait<Reg::kMethod>::message_type>;
};

/**
 * \brief Find the registered method by method only.
 * Cmpare all registered methods from type list is method same as kMethod.
 * If not found return NullRegMethod.
 */
template <auto method>
static consteval auto FindRegByMethod(TypeList<>) {
  return NullRegMethod{};
}

template <auto method, typename Reg, typename... Rest>
static consteval auto FindRegByMethod(TypeList<Reg, Rest...>) {
  if constexpr (AreMethodsSame<method, Reg::kMethod>::value) {
    return Reg{};
  } else {
    return FindRegByMethod<method>(TypeList<Rest...>{});
  }
}

/**
 * \brief Makes type list for api methods list.
 * It obtains the biggest size and alignment from all api messages from
 * RegMethods.
 */
template <std::size_t Capacity, typename... RegMethods>
static consteval auto GenPacketListForApi(TypeList<RegMethods...>) {
  auto max = [](auto... v) {
    std::size_t res{};
    ((res = res > v ? res : v), ...);
    return res;
  };

  constexpr auto max_size = max(
      sizeof(void*),
      sizeof(
          PackMessage<typename ApiMessageTrait<RegMethods>::message_type>)...);
  constexpr auto max_align = max(
      alignof(void*),
      alignof(
          PackMessage<typename ApiMessageTrait<RegMethods>::message_type>)...);

  return PacketList<max_size, max_align, Capacity>{};
}
}  // namespace api_class_declate_internal

using api_class_declate_internal::RegMethod;

#define METHOD(id, method) ::ae::RegMethod<id, &SelfType::method>

#define API_LIST(...)                                                         \
  template <typename SelfType>                                                \
  static consteval auto ApiList() -> ::ae::TypeList<__VA_ARGS__> {            \
    static_assert(                                                            \
        ::ae::api_class_declate_internal::AreApiMessageIdsValid(              \
            ::ae::TypeList<__VA_ARGS__>{}),                                   \
        "API message IDs must be unique and cannot use reserved IDs 0 or 1"); \
    return {};                                                                \
  }

template <typename Api>
class DeclareApi {
  friend Api;

 private:
  DeclareApi() = default;
  ~DeclareApi() = default;

  template <typename RegMethod>
    requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
  static void InvokeReturnResultApi(ServerApiContext& server_context);

  template <typename RegMethod>
    requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
  static void InvokeApi(DeclareApi& self, ServerApiContext& server_context);

  /**
   * \brief Invoke registered method
   */
  template <typename RegMethod>
    requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
  static void ServerInvokeMethod(DeclareApi& self,
                                 ServerApiContext& server_context);

  static bool ServerDispatchMethods(DeclareApi& self,
                                    ServerApiContext& server_context,
                                    MessageId id);

 public:
  using api_concept = Api;
  /**
   * \brief Make a packet list type suitable for current Api class method
   * list
   */
  template <std::size_t MaxCapacity>
  static consteval auto MakePacketList() {
    return api_class_declate_internal::GenPacketListForApi<MaxCapacity>(
        Api::template ApiList<Api>());
  }

  /**
   * \brief Call client's api meothod.
   * The method must be one of registered api methods in ApiList \see API_LIST
   */
  template <auto method, typename... Args>
  static decltype(auto) CallClientMethod(ClientApiContext& client_api_context,
                                         Args&&... args);

  /**
   * \brief Call client's api method.
   * Returns the same as client's method returns
   */
  template <auto method, typename... Args>
  decltype(auto) ClientMethod(Args&&... args);

  /**
   * \brief Load server's api method. Invoke the method with MessageId id and
   * return true if invoked, false if not.
   */
  bool LoadMethod(MessageId id);

  // Expose ReturnResultApi's methods

  template <typename T>
  void SendResult(RequestId id, T&& data);

  void SendError(RequestId id, std::uint8_t error_type,
                 std::int32_t error_code);

  void set_client_context(ClientApiContext& context) {
    client_context_ = &context;
  }
  void reset_client_context() { client_context_ = nullptr; }
  ClientApiContext& client_context() {
    assert(client_context_ != nullptr && "Context is null");
    return *client_context_;
  }

  void set_server_context(ServerApiContext& context) {
    server_context_ = &context;
  }
  void reset_server_context() { server_context_ = nullptr; }
  ServerApiContext& server_context() {
    assert(server_context_ != nullptr && "Context is null");
    return *server_context_;
  }

 private:
  ClientApiContext* client_context_ = nullptr;
  ServerApiContext* server_context_ = nullptr;
};

template <typename T>
concept DeclaredApi =
    requires { std::is_void_v<std::void_t<typename T::api_concept>>; };

template <typename Api>
template <typename RegMethod>
  requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
void DeclareApi<Api>::InvokeReturnResultApi(ServerApiContext& server_context) {
  if constexpr (std::is_same_v<ReturnResultApi::RegSendResult, RegMethod>) {
    ReturnResultApi::SendResultImpl(server_context);
  } else {
    ReturnResultApi::SendErrorImpl(server_context);
  }
}

template <typename Api>
template <typename RegMethod>
  requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
void DeclareApi<Api>::InvokeApi(DeclareApi& self,
                                ServerApiContext& server_context) {
  using Trait = api_class_declate_internal::MessageTrait<RegMethod::kMethod>;
  // message to read
  using M = TypeListToTemplate_t<GenericMessage, typename Trait::args_list>;
  auto& reader = server_context.reader();
  auto invoker = [&]<typename... Args>(Args&&... args) {
    // Invoke the api class method with extracted arguments
    return (static_cast<Api&>(self).*
            RegMethod::kMethod)(std::forward<Args>(args)...);
  };
  if constexpr (Trait::kReturnValue) {
    // setup request id into server method invoke context
    auto request_id = reader.TryExtract<RequestId>();
    if (!request_id) {
      reader.Cancel();
      return;
    }
    server_context.SetRequestId(*request_id);
    auto m = reader.TryExtract<M>();
    if (!m) {
      reader.Cancel();
      return;
    }
    ApiPromise<typename Trait::return_type> promise =
        std::apply(invoker, m->fields);
    // Currently the server api implementation class responsible for invoke
    // SendResult/SendError to actually return vale
    // Here it looks impossible
    // We need to switch from server method invoke context to client method
    // invoke context
    (void)promise;
  } else {
    auto m = reader.TryExtract<M>();
    if (!m) {
      reader.Cancel();
      return;
    }
    std::apply(invoker, m->fields);
  }
}

/**
 * \brief Invoke registered method
 */
template <typename Api>
template <typename RegMethod>
  requires(api_class_declate_internal::IsRegMethod<RegMethod>::value)
void DeclareApi<Api>::ServerInvokeMethod(DeclareApi& self,
                                         ServerApiContext& server_context) {
  if constexpr (std::is_same_v<ReturnResultApi::RegSendResult, RegMethod> ||
                std::is_same_v<ReturnResultApi::RegSendError, RegMethod>) {
    InvokeReturnResultApi<RegMethod>(server_context);
  } else {
    InvokeApi<RegMethod>(self, server_context);
  }
}

template <typename Api>
bool DeclareApi<Api>::ServerDispatchMethods(DeclareApi& self,
                                            ServerApiContext& server_context,
                                            MessageId id) {
  auto dispatcher = [&]<typename... Regs>(TypeList<Regs...>) {
    bool res = (((id == Regs::kMessageId)
                 ? (ServerInvokeMethod<Regs>(self, server_context)),
                 true : false) ||
                ...);
    return res;
  };
  return dispatcher(JoinedTypeList_t<decltype(ReturnResultApi::ServeApiList()),
                                     decltype(Api::template ApiList<Api>())>{});
}

template <typename Api>
template <auto method, typename... Args>
decltype(auto) DeclareApi<Api>::CallClientMethod(
    ClientApiContext& client_api_context, Args&&... args) {
  using Reg = decltype(api_class_declate_internal::FindRegByMethod<method>(
      Api::template ApiList<Api>()));
  static_assert(
      !std::is_same_v<Reg, api_class_declate_internal::NullRegMethod>);

  using Trait = api_class_declate_internal::MessageTrait<Reg::kMethod>;
  using AeTrait = api_class_declate_internal::ApiMessageTrait<Reg>;
  auto& packet_list = client_api_context.packet_list();

  if constexpr (Trait::kReturnValue) {
    auto& protocol_context = client_api_context.protocol_context();
    auto id = protocol_context.NextRequestId();
    packet_list.Push(typename AeTrait::message_type{
        typename Trait::message_type{id, std::forward<Args>(args)...}});
    auto& future =
        protocol_context.template CreateFuture<typename Trait::return_type>(id);
    return ApiPromise{future};
  } else {
    packet_list.Push(typename AeTrait::message_type{
        typename Trait::message_type{std::forward<Args>(args)...}});
  }
}

template <typename Api>
template <auto method, typename... Args>
decltype(auto) DeclareApi<Api>::ClientMethod(Args&&... args) {
  assert(client_context_ != nullptr && "Call client api without context");
  return CallClientMethod<method>(*client_context_,
                                  std::forward<Args>(args)...);
}

template <typename Api>
bool DeclareApi<Api>::LoadMethod(MessageId id) {
  assert(server_context_ != nullptr && "Load method without context");
  return ServerDispatchMethods(*this, *server_context_, id);
}

template <typename Api>
template <typename T>
void DeclareApi<Api>::SendResult(RequestId id, T&& data) {
  assert(client_context_ != nullptr && "Call client api without context");
  ReturnResultApi::SendResult(*client_context_, id, std::forward<T>(data));
}

template <typename Api>
void DeclareApi<Api>::SendError(RequestId id, std::uint8_t error_type,
                                std::int32_t error_code) {
  assert(client_context_ != nullptr && "Call client api without context");
  ReturnResultApi::SendError(*client_context_, id, error_type, error_code);
}

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_API_CLASS_DECLARE_H_
