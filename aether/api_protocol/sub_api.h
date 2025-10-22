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

#ifndef AETHER_API_PROTOCOL_SUB_API_H_
#define AETHER_API_PROTOCOL_SUB_API_H_

#include <vector>
#include <functional>

#include "aether/type_traits.h"
#include "aether/types/type_list.h"
#include "aether/reflect/reflect.h"
#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/api_pack_parser.h"

namespace ae {
namespace sub_api_internal {
template <typename T>
struct ApiContextApiType;

template <typename T>
struct ApiContextApiType<ApiContext<T>> {
  using type = T;
};
}  // namespace sub_api_internal

template <typename TApi>
class SubApi {
 public:
  using Method = std::function<void(ApiContext<TApi>& api)>;

  template <typename TFunc, AE_REQUIRERS_NOT((std::is_same<SubApi, TFunc>))>
  explicit SubApi(TFunc&& caller) : caller_{std::forward<TFunc>(caller)} {}
  explicit SubApi(Method caller) : caller_{std::move(caller)} {}

  std::vector<std::uint8_t> operator()(TApi& api) const {
    auto api_context = ApiContext{api};
    caller_(api_context);
    return std::move(api_context);
  }

 private:
  Method caller_;
};

template <typename TFunc>
SubApi(TFunc&&) -> SubApi<typename sub_api_internal::ApiContextApiType<
    std::decay_t<TypeAtT<0, typename FunctionSignature<TFunc>::Args>>>::type>;

template <typename TApi>
class SubApiImpl {
 public:
  struct PassThrough {
    std::vector<std::uint8_t> const& operator()(
        std::vector<std::uint8_t> const& d) {
      return d;
    }
  };

  SubApiImpl() = default;

  template <typename TFunc = PassThrough>
  void Parse(TApi& api, TFunc pre_parse = {}) {
    auto&& mod_data = pre_parse(data_);
    ApiParser parser{api.protocol_context(), mod_data};
    parser.Parse(api);
  }

  AE_REFLECT_MEMBERS(data_);

 private:
  std::vector<std::uint8_t> data_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_SUB_API_H_
