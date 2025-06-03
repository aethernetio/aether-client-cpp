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

#include "aether/common.h"
#include "aether/tele/env/compiler.h"
#include "aether/type_traits.h"

#if defined __GNUC__ && !defined __clang__
#  if COMPILER_VERSION_NUM <= 1320
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=83258
#    define AE_HAS_GCC_NO_LINKAGE_POINTER_BUG 1
#  endif
#endif

namespace ae {

// Helper to pass function pointer into delegate
template <auto Method>
struct MethodPtr {
  static constexpr decltype(Method) kMethod = Method;
};

// Make a MethodPtr<Method> out of Functor convertible to function pointer
template <
    typename TFunctor,
    typename FSignature = FunctionSignature<decltype(&TFunctor::operator())>,
    AE_REQUIRERS((IsFunctionPtr<typename FSignature::FuncPtr, TFunctor>))>
constexpr auto FunctorPtr(TFunctor func) {
  return MethodPtr<static_cast<typename FSignature::FuncPtr>(func)>{};
}

// Make a MethodPtr<Method> out of Functor's operator()
template <
    typename TFunctor,
    typename FSignature = FunctionSignature<decltype(&TFunctor::operator())>,
    AE_REQUIRERS_NOT((IsFunctionPtr<typename FSignature::FuncPtr, TFunctor>))>
constexpr auto FunctorPtr(TFunctor /* func */) {
  return MethodPtr<&TFunctor::operator()>{};
}

template <typename Signature>
class Delegate;

/**
 * \brief Make a not owning functor based on function pointer, functor, or
 * class member function pointer and it's instance.
 *
 * Not owning means no instance is copied or moved into Delegate, it stores
 * only pointers. User must use objects with proper lifetime only, otherwise
 * the behavior is undefined. All provided function pointers should be
 * constexpr. MethodPtr is used as helper to pass such pointers.
 */
template <typename TRet, typename... TArgs>
class Delegate<TRet(TArgs...)> {
  using VirtualCallFunc = TRet (*)(void* instance, TArgs&&...);

 public:
  Delegate() = default;

  // Init with function pointer
  template <TRet (*FuncPtr)(TArgs...)>
  constexpr explicit Delegate(
      MethodPtr<FuncPtr> const& /* method_ptr */) noexcept
      : instance_{}, v_call_func_{CallFunctionPointer<FuncPtr>} {}

#if !defined AE_HAS_GCC_NO_LINKAGE_POINTER_BUG
  // For no capturing functor objects
  template <typename TFunctor,
            AE_REQUIRERS((IsFunctionPtr<TRet (*)(TArgs...), TFunctor>))>
  constexpr explicit Delegate(TFunctor functor) noexcept
      : instance_{},
        v_call_func_{CallFunctionPointer<FunctorPtr(functor).kMethod>} {}
  // For regular functor objects
  template <typename TFunctor,
            AE_REQUIRERS_NOT((IsFunctionPtr<TRet (*)(TArgs...), TFunctor>))>
  constexpr explicit Delegate(TFunctor& functor) noexcept
      : instance_{&functor}, v_call_func_{CallFunctor<TFunctor>} {}
#else
  // For regular functor objects
  template <typename TFunctor>
  constexpr explicit Delegate(TFunctor& functor) noexcept
      : instance_{&functor}, v_call_func_{CallFunctor<TFunctor>} {}
#endif

  // For pointer to member function
  template <typename TClass, TRet (TClass::*Method)(TArgs...)>
  constexpr explicit Delegate(
      TClass& instance, MethodPtr<Method> const& /* method_ptr */) noexcept
      : instance_{&instance}, v_call_func_{CallClassMember<TClass, Method>} {}

  template <typename TClass, TRet (TClass::*Method)(TArgs...) noexcept>
  constexpr explicit Delegate(
      TClass& instance, MethodPtr<Method> const& /* method_ptr */) noexcept
      : Delegate{instance,
                 MethodPtr<static_cast<TRet (TClass::*)(TArgs...)>(Method)>{}} {
  }

  // For pointer to const member function
  template <typename TClass, TRet (TClass::*Method)(TArgs...) const>
  constexpr explicit Delegate(
      TClass const& instance,
      MethodPtr<Method> const& /* method_ptr */) noexcept
      : instance_{const_cast<TClass*>(&instance)},
        v_call_func_{CallClassMemberConst<TClass, Method>} {}

  // it's trivially copy/moveable
  AE_CLASS_COPY_MOVE(Delegate)

  constexpr TRet operator()(TArgs... args) const noexcept {
    return v_call_func_(instance_, std::forward<TArgs>(args)...);
  }

 private:
  template <TRet (*func_pointer)(TArgs...)>
  static TRet CallFunctionPointer(void* /*instance*/, TArgs&&... args) {
    return func_pointer(std::forward<TArgs>(args)...);
  }

  template <typename TFunctor>
  static TRet CallFunctor(void* instance, TArgs&&... args) {
    auto* functor_instance = static_cast<TFunctor*>(instance);
    return functor_instance->operator()(std::forward<TArgs>(args)...);
  }

  template <typename TClass, TRet (TClass::*Method)(TArgs...)>
  static TRet CallClassMember(void* instance, TArgs&&... args) {
    auto* class_instance = static_cast<TClass*>(instance);
    return (class_instance->*Method)(std::forward<TArgs>(args)...);
  }

  template <typename TClass, TRet (TClass::*Method)(TArgs...) const>
  static TRet CallClassMemberConst(void* instance, TArgs&&... args) {
    auto const* class_instance =
        const_cast<TClass const*>(static_cast<TClass*>(instance));
    return (class_instance->*Method)(std::forward<TArgs>(args)...);
  }

  void* instance_;               // instance
  VirtualCallFunc v_call_func_;  // pointer to caller function
};

template <auto Method>
Delegate(MethodPtr<Method> const&)
    -> Delegate<typename FunctionSignature<decltype(Method)>::Signature>;

template <typename TFunctor>
Delegate(TFunctor) -> Delegate<
    typename FunctionSignature<decltype(&TFunctor::operator())>::Signature>;

template <typename TClass, typename TMethodType>
Delegate(TClass&, TMethodType const&) -> Delegate<
    typename FunctionSignature<decltype(TMethodType::kMethod)>::Signature>;

}  // namespace ae

#endif  // AETHER_EVENTS_DELEGATE_H_
