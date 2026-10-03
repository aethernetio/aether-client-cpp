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

#ifndef AETHER_API_PROTOCOL_DETAILS_SUB_API_H_
#define AETHER_API_PROTOCOL_DETAILS_SUB_API_H_

#include <variant>

#include "aether-miscpp/meta/function_signature.h"
#include "aether-miscpp/meta/type_list.h"
#include "aether-miscpp/misc/override.h"
#include "aether-miscpp/types/small_function.h"

#include "aether/types/data_buffer.h"

#include "aether/api_protocol/details/api_class_declare.h"
#include "aether/api_protocol/details/api_context.h"
#include "aether/api_protocol/details/api_internal_context.h"

namespace ae {
namespace sub_api_internal {
template <typename T>
struct ApiContextApiType;

template <typename T>
struct ApiContextApiType<ApiContext<T>> {
  using type = T;
};
}  // namespace sub_api_internal

/**
 * \brief Sub api caller
 * For client it invokes Api methods in api context.
 */
template <typename Api>
class SubApi {
 public:
  static_assert(DeclaredApi<Api>, "Api must be declared api");
  using Method = SmallFunction<void(ApiContext<Api>& api)>;

  // client variant
  // init with ApiContext caller or Method
  template <typename TFunc>
    requires(!std::same_as<SubApi, std::decay_t<TFunc>>)
  SubApi(TFunc&& caller)  // NOLINT(*explicit*)
      : var_{std::in_place_index<1>, std::forward<TFunc>(caller)} {}
  SubApi(Method caller)  // NOLINT(*explicit*)
      : var_{std::in_place_index<1>, std::move(caller)} {}
  // init with filled api context
  SubApi(ApiContext<Api>&& api_context)  // NOLINT(*explicit*)
      : var_{std::in_place_index<0>, std::move(api_context).Pack()} {}

  // server variant
  // init with just data buffer
  SubApi(DataBuffer buffer)  // NOLINT(*explicit*)
      : var_{std::in_place_index<0>, std::move(buffer)} {}

  // client variant with api and external client context
  // invoke caller with api class or return stored buffer
  template <typename A>
    requires(std::same_as<A, Api> || std::derived_from<A, Api>)
  DataBuffer operator()(A& api, ClientApiContext& client_context) && {
    // protocol context must be used only for method invoke
    return std::move(*this).Visit([&](Method&& method) {
      auto ac = ApiContext<Api>{api, client_context.protocol_context()};
      std::move(method)(ac);
      return std::move(ac).Pack();
    });
  }
  // client variant with external maded api context
  // invoke caller with api class or return stored buffer
  DataBuffer operator()(ApiContext<Api>&& api_context) && {
    return std::move(*this).Visit([&](Method&& method) {
      std::move(method)(api_context);
      return std::move(api_context).Pack();
    });
  }

  // server variant
  // return the stored buffer
  DataBuffer&& buffer() && {
    assert(var_.index() == 0 && "Server variant must contain a data buffer");
    return std::move(std::get<DataBuffer>(var_));
  }

 private:
  template <typename MethodInvoker>
  DataBuffer Visit(MethodInvoker&& method_invoker) && {
    return std::visit(
        Override{
            std::forward<MethodInvoker>(method_invoker),
            [](DataBuffer&& data_buffer) noexcept {
              return std::move(data_buffer);
            },
        },
        std::move(var_));
  }

  std::variant<DataBuffer, Method> var_;
};

template <typename Api>
SubApi(ApiContext<Api>&&) -> SubApi<Api>;

template <typename TFunc>
SubApi(TFunc&&) -> SubApi<typename sub_api_internal::ApiContextApiType<
    std::decay_t<TypeAt_t<0, typename FunctionSignature<TFunc>::args>>>::type>;
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_SUB_API_H_
