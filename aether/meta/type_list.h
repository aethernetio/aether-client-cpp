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

#ifndef AETHER_META_TYPE_LIST_H_
#define AETHER_META_TYPE_LIST_H_

#include <cstddef>
#include <utility>
#include <type_traits>

#include "aether/meta/index_sequence.h"

namespace ae {
template <typename... T>
struct TypeList {};

template <typename... T>
struct TypeListMaker {
  using type = TypeList<std::decay_t<T>...>;

  template <typename... U>
  explicit constexpr TypeListMaker(U&&...) {}
};

template <typename... T>
TypeListMaker(T&&...) -> TypeListMaker<T...>;

namespace type_list_internal {
template <std::size_t I>
using index_constant = std::integral_constant<std::size_t, I>;

template <std::size_t I, typename U>
struct NType {
  static auto get(index_constant<I>) -> U;
};

template <typename... Ts>
struct NTypeSelector {
  template <typename Indices>
  struct Selector;

  template <std::size_t... Is>
  struct Selector<std::index_sequence<Is...>> : NType<Is, Ts>... {
    using NType<Is, Ts>::get...;
  };

  template <std::size_t I>
  static auto get()
      -> decltype(Selector<decltype(std::index_sequence_for<Ts...>())>::get(
          index_constant<I>{}));

  template <std::size_t I>
  using type = decltype(get<I>());
};

}  // namespace type_list_internal

template <std::size_t I, typename TList>
struct TypeAt;

template <std::size_t I, typename... Ts>
struct TypeAt<I, TypeList<Ts...>> {
  using type =
      typename type_list_internal::NTypeSelector<Ts...>::template type<I>;
};

template <std::size_t I, typename TList>
using TypeAt_t = typename TypeAt<I, TList>::type;

template <typename... Ts>
struct TypeListSize;

template <typename... Ts>
struct TypeListSize<TypeList<Ts...>> {
  static constexpr std::size_t value = sizeof...(Ts);
};

template <typename TList>
static inline constexpr std::size_t TypeListSize_v = TypeListSize<TList>::value;

/**
 * \brief Join two type lists.
 */
template <typename T1, typename T2>
struct JoinedTypeList;

template <typename... T1, typename... T2>
struct JoinedTypeList<TypeList<T1...>, TypeList<T2...>> {
  using type = TypeList<T1..., T2...>;
};

template <typename List1, typename List2>
using JoinedTypeList_t =
    typename JoinedTypeList<std::decay_t<List1>, std::decay_t<List2>>::type;

/**
 * \brief Pass type list as template parameters to template T
 */
template <template <typename...> typename T, typename TList>
struct TypeListToTemplate;

template <template <typename...> typename T, typename... Ts>
struct TypeListToTemplate<T, TypeList<Ts...>> {
  using type = T<Ts...>;
};

/**
 * \brief Revers type list
 */
template <typename T>
struct ReversTypeList;

template <typename... Ts>
struct ReversTypeList<TypeList<Ts...>> {
  using n_type_selector = type_list_internal::NTypeSelector<Ts...>;

  template <std::size_t... Is>
  static auto ReselectTypes(std::index_sequence<Is...>)
      -> TypeList<typename n_type_selector::template type<Is>...>;

  using type = decltype(ReselectTypes(
      reverse_sequence(std::index_sequence_for<Ts...>{})));
};

template <typename T>
struct ReversTypeList<TypeList<T>> {
  using type = TypeList<T>;
};

}  // namespace ae
#endif  // AETHER_META_TYPE_LIST_H_
