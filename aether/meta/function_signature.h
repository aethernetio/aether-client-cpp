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

#ifndef AETHER_META_FUNCTION_SIGNATURE_H_
#define AETHER_META_FUNCTION_SIGNATURE_H_

#include <utility>

#include "aether/meta/type_list.h"

namespace ae {
template <typename Ret, typename... Args>
struct FunctionSignatureImpl {
  using ret = Ret;
  using args = TypeList<Args...>;
  using signature = ret(args...);
  using func_ptr = ret (*)(args...);
};

template <typename R, typename... Args>
auto GetFunctionSignature(R (*)(Args...)) -> FunctionSignatureImpl<R, Args...>;

template <typename R, typename C, typename... Args>
auto GetFunctionSignature(R (C::*)(Args...))
    -> FunctionSignatureImpl<R, Args...>;

template <typename R, typename C, typename... Args>
auto GetFunctionSignature(R (C::*)(Args...) const)
    -> FunctionSignatureImpl<R, Args...>;

template <typename Callable>
auto GetFunctionSignature(Callable)
    -> decltype(GetFunctionSignature(&Callable::operator()));

template <typename Func>
struct FunctionSignature
    : decltype(GetFunctionSignature(std::declval<Func>())){};

}  // namespace ae

#endif  // AETHER_META_FUNCTION_SIGNATURE_H_
