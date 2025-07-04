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

#ifndef AETHER_REFLECT_REFLECT_IMPL_H_
#define AETHER_REFLECT_REFLECT_IMPL_H_

#include <cstddef>
#include <utility>
#include <type_traits>

#include "aether/common.h"
#include "aether/types/type_list.h"  // IWYU pragma: keep

namespace ae::reflect {
namespace reflect_internal {
template <typename T, auto val>
struct FieldPtr;

/**
 * \brief Pointer to class member field.
 */
template <typename T, typename M, M T::* val>
struct FieldPtr<T, val> {
  using class_type = T;
  using type = M;
  static constexpr auto kValue = val;

  template <typename U, AE_REQUIRERS((std::is_same<T, std::decay_t<U>>))>
  static constexpr decltype(auto) get(U&& obj) {
    return std::forward<U>(obj).*kValue;
  }
};

/**
 * \brief Field getter from getter type
 */
template <typename T, typename TGetter>
struct FieldGetter {
  using class_type = T;
  using getter = TGetter;
  using type = std::remove_pointer_t<
      std::decay_t<decltype(getter::get(std::declval<class_type*>()))>>;

  template <typename U,
            AE_REQUIRERS((std::is_same<class_type, std::decay_t<U>>))>
  static constexpr decltype(auto) get(U&& obj) {
    if constexpr (std::is_const_v<std::remove_reference_t<U>>) {
      return const_cast<type const&>(
          *getter::get(const_cast<class_type*>(&std::forward<U>(obj))));
    } else {
      return *getter::get(&std::forward<U>(obj));
    }
  }
};

/**
 * \brief class T's member fields list
 */
template <typename T, typename... TFields>
struct FieldList {
  using Type = T;
  using FieldsTypeList = TypeList<TFields...>;
  static constexpr std::size_t kSize = sizeof...(TFields);

  /**
   * \brief Apply func to obj fields
   */
  template <typename U, typename Func>
  static constexpr void Apply(U&& obj, Func&& func) {
    // apply to each field
    std::forward<Func>(func)(TFields::get(std::forward<U>(obj))...);
  }

  /**
   * \brief Get field
   */
  template <std::size_t I, typename U>
  static constexpr decltype(auto) get(U&& obj) {
    return TypeAtT<I, FieldsTypeList>::get(std::forward<U>(obj));
  }
};

template <typename T, typename FieldList>
class ReflectionImpl {
 public:
  using FieldListType = typename FieldList::Type;

  constexpr explicit ReflectionImpl(T obj) : obj_{std::forward<T>(obj)} {}

  template <typename Func>
  constexpr void Apply(Func&& func) {
    FieldList::Apply(obj_, std::forward<Func>(func));
  }

  template <std::size_t I>
  constexpr auto& get() {
    return FieldList::template get<I>(obj_);
  }

  constexpr std::size_t size() const { return FieldList::kSize; }

 private:
  T obj_;
};
}  // namespace reflect_internal

template <typename T, typename Enable = void>
class Reflection;

template <typename T>
class Reflection<T, std::void_t<decltype(std::declval<T>().FieldList())>>
    : reflect_internal::ReflectionImpl<
          T, decltype(std::declval<T>().FieldList())> {
 public:
  using Impl =
      reflect_internal::ReflectionImpl<T,
                                       decltype(std::declval<T>().FieldList())>;
  static_assert(std::is_same_v<std::decay_t<T>, typename Impl::FieldListType>,
                "Reflection should not be derived");

  constexpr explicit Reflection(T obj) : Impl{std::forward<T>(obj)} {}

  using Impl::Apply;
  using Impl::get;
  using Impl::size;
};

template <typename T>
Reflection(T&&) -> Reflection<T>;

template <typename T, typename Enable = void>
struct IsReflectable : std::false_type {};

template <typename T>
struct IsReflectable<T,
                     std::void_t<decltype(Reflection<T&>{std::declval<T&>()})>>
    : std::true_type {};

}  // namespace ae::reflect

#define _AE_APPLY_MACRO_0(MACRO, ...)
#define _AE_APPLY_MACRO_1(MACRO, ARG, ...) \
  MACRO(ARG) _AE_APPLY_MACRO_0(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_2(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_1(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_3(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_2(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_4(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_3(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_5(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_4(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_6(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_5(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_7(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_6(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_8(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_7(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_9(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_8(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_10(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_9(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_11(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_10(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_12(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_11(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_13(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_12(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_14(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_13(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_15(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_14(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_16(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_15(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_17(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_16(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_18(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_17(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_19(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_18(MACRO, __VA_ARGS__)
#define _AE_APPLY_MACRO_20(MACRO, ARG, ...) \
  MACRO(ARG), _AE_APPLY_MACRO_19(MACRO, __VA_ARGS__)

#define _AE_APPLY_MACRO_N(MACRO, _20, _19, _18, _17, _16, _15, _14, _13, _12, \
                          _11, _10, _9, _8, _7, _6, _5, _4, _3, _2, _1, X,    \
                          ...)                                                \
  _AE_APPLY_MACRO##X(MACRO, _20, _19, _18, _17, _16, _15, _14, _13, _12, _11, \
                     _10, _9, _8, _7, _6, _5, _4, _3, _2, _1, _0)

#define _AE_APPLY_MACRO(MACRO, ...)                                         \
  _AE_APPLY_MACRO_N(MACRO, __VA_ARGS__, _20, _19, _18, _17, _16, _15, _14,  \
                    _13, _12, _11, _10, _9, _8, _7, _6, _5, _4, _3, _2, _1, \
                    _0)

/**
 * \brief Provide access to regular class member through pointer to member.
 */
#define AE_MMBR(MEMBER) \
  ::ae::reflect::reflect_internal::FieldPtr<SelfType, &SelfType::MEMBER> {}

/**
 * \brief Provide access to regular class member through pointer to member.
 */
#define AE_MMBRS(...) _AE_APPLY_MACRO(AE_MMBR, __VA_ARGS__)

/**
 * \brief Provide access to reference type member.
 * c++ forbids create pointers to members if it's reference type.
 */
#define AE_REF(MEMBER)                                                       \
  []() constexpr {                                                           \
    struct Getter {                                                          \
      static decltype(auto) get(SelfType* obj) { return &obj->MEMBER; }      \
    };                                                                       \
    return ::ae::reflect::reflect_internal::FieldGetter<SelfType, Getter>{}; \
  }()

/**
 * \brief Provide a member as reference to Base class.
 */
#define AE_REF_BASE(Base)                                                    \
  []() constexpr {                                                           \
    struct Getter {                                                          \
      static decltype(auto) get(SelfType* obj) {                             \
        return static_cast<Base*>(obj);                                      \
      }                                                                      \
    };                                                                       \
    return ::ae::reflect::reflect_internal::FieldGetter<SelfType, Getter>{}; \
  }()

/**
 * \brief Make type reflectable with manual members marking.
 * Use AE_MMBR, AE_REF, AE_REF_BASE to mark members.
 */
#define AE_REFLECT(...)                                                       \
  constexpr auto FieldList() const {                                          \
    using SelfType = std::decay_t<decltype(*this)>;                           \
    static_assert(sizeof(SelfType) != 0);                                     \
    constexpr auto fields = ::ae::TypeList{__VA_ARGS__};                      \
    using TypeFieldTypeList =                                                 \
        ::ae::JoinLists<::ae::TypeList<SelfType>, decltype(fields)>;          \
    using FL = typename ::ae::TypeListToTemplate<                             \
        ::ae::reflect::reflect_internal::FieldList, TypeFieldTypeList>::type; \
    return FL{};                                                              \
  }

/**
 * \brief Make type reflectable with each listed member marked automatically as
 * class member.
 */
#define AE_REFLECT_MEMBERS(...)                                               \
  constexpr auto FieldList() const {                                          \
    using SelfType = std::decay_t<decltype(*this)>;                           \
    static_assert(sizeof(SelfType) != 0);                                     \
    constexpr auto fields =                                                   \
        ::ae::TypeList{_AE_APPLY_MACRO(AE_MMBR, __VA_ARGS__)};                \
    using TypeFieldTypeList =                                                 \
        ::ae::JoinLists<::ae::TypeList<SelfType>, decltype(fields)>;          \
    using FL = typename ::ae::TypeListToTemplate<                             \
        ::ae::reflect::reflect_internal::FieldList, TypeFieldTypeList>::type; \
    return FL{};                                                              \
  }

#endif  // AETHER_REFLECT_REFLECT_IMPL_H_
