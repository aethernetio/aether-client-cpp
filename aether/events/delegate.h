/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef AETHER_EVENTS_DELEGATE_H_
#define AETHER_EVENTS_DELEGATE_H_

#include <utility>

namespace ae {

template <auto Method>
struct MethodPtr {
  static constexpr decltype(Method) kMethod = Method;
};

template <typename Signature>
class Delegate;

template <typename TRet, typename... TArgs>
class Delegate<TRet(TArgs...)> {
  using VirtualCallFunc = TRet (*)(void* instance, TArgs...);

 public:
  Delegate() = default;

  template <typename TLambda>
  constexpr explicit Delegate(TLambda& lambda) noexcept
      : instance_{&lambda}, v_call_func_{CallLambda<TLambda>} {}

  template <typename TClass, TRet (TClass::*Method)(TArgs...)>
  constexpr explicit Delegate(TClass& instance,
                              MethodPtr<Method> const&) noexcept
      : instance_{&instance}, v_call_func_{CallClassMember<TClass, Method>} {}

  TRet operator()(TArgs... args) const noexcept {
    return Invoke(std::forward<TArgs>(args)...);
  }

 private:
  TRet Invoke(TArgs... args) const noexcept {
    return v_call_func_(instance_, std::forward<TArgs>(args)...);
  }

  template <typename TLambda>
  static TRet CallLambda(void* instance, TArgs... args) {
    auto* lambda_instance = static_cast<TLambda*>(instance);
    return lambda_instance->operator()(std::forward<TArgs>(args)...);
  }

  template <typename TClass, TRet (TClass::*Method)(TArgs...)>
  static TRet CallClassMember(void* instance, TArgs... args) {
    auto* class_instance = static_cast<TClass*>(instance);
    return (class_instance->*Method)(std::forward<TArgs>(args)...);
  }

  void* instance_{};               // instance
  VirtualCallFunc v_call_func_{};  // pointer to caller function
};

}  // namespace ae

#endif  // AETHER_EVENTS_DELEGATE_H_
