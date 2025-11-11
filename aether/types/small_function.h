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

#ifndef AETHER_TYPES_SMALL_FUNCTION_H_
#define AETHER_TYPES_SMALL_FUNCTION_H_

#include <memory>
#include <cstddef>
#include <type_traits>

#include "aether/common.h"
#include "aether/type_traits.h"
#include "aether/types/aligned_storage.h"

namespace ae {
namespace small_function_internal {
enum class Operation : std::uint8_t {
  kMove,
  kDestroy,
};

template <typename TRet, typename... TArgs>
struct VTable {
  TRet (*invoke)(void*, TArgs&&...);
  void (*manage)(void*, void*, Operation);
};

template <typename TRet, typename... TArgs>
struct Invoker {
  // Invoke for callable objects
  template <typename TCallable>
  static TRet InvokeCallable(void* self, TArgs&&... args) {
    if constexpr (std::is_void_v<TRet>) {
      std::launder(static_cast<TCallable*>(self))
          ->operator()(std::forward<TArgs>(args)...);
    } else {
      return std::launder(static_cast<TCallable*>(self))
          ->operator()(std::forward<TArgs>(args)...);
    }
  }

  // Invoke for member functions
  template <typename T, auto Method>
  static TRet InvokeMemberFunction(void* self, TArgs&&... args) {
    auto* instance = reinterpret_cast<T*>(*static_cast<std::uintptr_t*>(self));
    return (instance->*Method)(std::forward<TArgs>(args)...);
  }

  // Invoke for free functions
  static TRet InvokeFreeFunction(void* self, TArgs&&... args) {
    auto method = reinterpret_cast<TRet (*)(TArgs...)>(
        *static_cast<std::uintptr_t*>(self));
    return method(std::forward<TArgs>(args)...);
  }
};

template <typename TCallable>
static void Manage(void* a, void* b, Operation op) noexcept {
  switch (op) {
    case Operation::kMove: {
      {
        if constexpr (std::is_trivially_move_constructible_v<TCallable>) {
          std::memcpy(b, a, sizeof(TCallable));
        } else {
          new (b)
              TCallable{std::move(*std::launder(static_cast<TCallable*>(a)))};
        }
        break;
      }
      case Operation::kDestroy: {
        if constexpr (!std::is_trivially_destructible_v<TCallable>) {
          std::destroy_at(std::launder(static_cast<TCallable*>(a)));
        }
        break;
      }
    }
  }
}

template <typename TCallable, typename TRet, typename... TArgs>
static constexpr auto VTableForT = VTable<TRet, TArgs...>{
    Invoker<TRet, TArgs...>::template InvokeCallable<TCallable>,
    Manage<TCallable>,
};

template <typename T, auto Method, typename TRet, typename... TArgs>
static constexpr auto VTableForClassMember = VTable<TRet, TArgs...>{
    Invoker<TRet, TArgs...>::template InvokeMemberFunction<T, Method>,
    Manage<T*>,
};

template <typename TRet, typename... TArgs>
static constexpr auto VTableForFreeFunction = VTable<TRet, TArgs...>{
    Invoker<TRet, TArgs...>::InvokeFreeFunction,
    Manage<TRet (*)(TArgs...)>,
};
}  // namespace small_function_internal

template <auto Method>
struct MethodPtr;

// class member function pointer
template <typename T, typename TRet, typename... TArgs,
          TRet (T::*Method)(TArgs...)>
struct MethodPtr<Method> {
  static constexpr auto kMethod = Method;
  T* instance;
};

// class member function pointer const
template <typename T, typename TRet, typename... TArgs,
          TRet (T::*Method)(TArgs...) const>
struct MethodPtr<Method> {
  static constexpr auto kMethod = Method;
  T* instance;
};

static constexpr std::size_t kDefaultSize = sizeof(void*) * 4;
static constexpr std::size_t kDefaultAlignment = alignof(void*);

template <typename Signature, std::size_t Size = kDefaultSize,
          std::size_t Alignment = kDefaultAlignment>
class SmallFunction;

template <typename TRet, typename... TArgs, std::size_t Size,
          std::size_t Alignment>
class SmallFunction<TRet(TArgs...), Size, Alignment> {
  using Storage = AlignedStorage<Size, Alignment>;
  using VTable = small_function_internal::VTable<TRet, TArgs...>;
  using Operation = small_function_internal::Operation;

 public:
  SmallFunction() = default;

  ~SmallFunction() noexcept {
    if (vtable_ != nullptr) {
      Destroy();
    }
  }

  template <
      typename TFunctor,
      AE_REQUIRERS((IsFunctor<std::decay_t<TFunctor>, TRet(TArgs...)>)),
      AE_REQUIRERS_NOT((std::is_same<SmallFunction, std::decay_t<TFunctor>>))>
  SmallFunction(TFunctor&& functor) noexcept
      : vtable_{&small_function_internal::VTableForT<std::decay_t<TFunctor>,
                                                     TRet, TArgs...>} {
    using Type = std::decay_t<TFunctor>;
    static_assert(sizeof(Type) > 0, "TFunctor must be a complete type");
    static_assert(sizeof(Type) <= Size, "TFunctor must fit into storage");

    new (storage_.data()) Type{std::forward<TFunctor>(functor)};
  }

  SmallFunction(TRet (*func_ptr)(TArgs...)) noexcept
      : vtable_{
            &small_function_internal::VTableForFreeFunction<TRet, TArgs...>} {
    using FuncType = decltype(func_ptr);
    static_assert(sizeof(FuncType) <= Size, "pointer must fit into storage");
    new (storage_.data()) FuncType{func_ptr};
  }

  template <typename T, typename... UArgs, TRet (T::*Method)(UArgs...)>
  SmallFunction(MethodPtr<Method> func_ptr) noexcept
      : vtable_{&small_function_internal::VTableForClassMember<T, Method, TRet,
                                                               TArgs...>} {
    static_assert(Size >= sizeof(T*), "pointer must fit into storage");
    new (storage_.data()) T* {func_ptr.instance};
  }

  template <typename T, typename... UArgs, TRet (T::*Method)(UArgs...) const>
  SmallFunction(MethodPtr<Method> func_ptr) noexcept
      : vtable_{&small_function_internal::VTableForClassMember<T, Method, TRet,
                                                               TArgs...>} {
    static_assert(Size >= sizeof(T*), "pointer must fit into storage");
    new (storage_.data()) T* {func_ptr.instance};
  }

  SmallFunction(SmallFunction const& other) = delete;

  SmallFunction(SmallFunction&& other) noexcept : vtable_{other.vtable_} {
    if (vtable_ != nullptr) {
      Move(other.storage_, storage_);
      other.Destroy();
    }
    other.vtable_ = nullptr;
  }

  SmallFunction& operator=(SmallFunction const& other) = delete;

  SmallFunction& operator=(SmallFunction&& other) noexcept {
    if (this != &other) {
      if (vtable_ != nullptr) {
        Destroy();
      }
      vtable_ = other.vtable_;
      if (vtable_ != nullptr) {
        Move(other.storage_, storage_);
        other.Destroy();
      }
      other.vtable_ = nullptr;
    }
    return *this;
  }

  /**
   * \brief Call operator
   */
  TRet operator()(TArgs... args) const {
    assert(vtable_ != nullptr && "SmallFunction is not initialized");
    return vtable_->invoke(const_cast<std::uint8_t*>(storage_.data()),
                           std::forward<TArgs>(args)...);
  }

  explicit operator bool() const noexcept { return vtable_ != nullptr; }

 private:
  void Destroy() noexcept {
    vtable_->manage(storage_.data(), nullptr, Operation::kDestroy);
  }
  void Move(Storage& src, Storage& dst) noexcept {
    vtable_->manage(src.data(), dst.data(), Operation::kMove);
  }

  VTable const* vtable_{};
  Storage storage_{};
};

template <typename TFunction>
SmallFunction(TFunction&&)
    -> SmallFunction<typename FunctionSignature<TFunction>::Signature>;

}  // namespace ae

#endif  // AETHER_TYPES_SMALL_FUNCTION_H_
