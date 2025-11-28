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

#ifndef AETHER_TYPES_TYPE_LIST_H_
#define AETHER_TYPES_TYPE_LIST_H_

#include <cstddef>
#include <utility>

namespace ae {
/**
 * \brief List of types used in metaprogramming.
 */
template <typename... T>
struct TypeList {
  TypeList() = default;
  template <typename... U>
  explicit constexpr TypeList(U&&...) {}
};

template <typename... T>
TypeList(T&&...) -> TypeList<T...>;

template <typename... Ts>
struct TypeListSizeImpl;

template <typename... Ts>
struct TypeListSizeImpl<TypeList<Ts...>> {
  static constexpr std::size_t value = sizeof...(Ts);
};

template <typename TList>
static constexpr std::size_t TypeListSize = TypeListSizeImpl<TList>::value;

/**
 * \brief Join two type lists.
 */
template <typename... T1s, typename... T2s>
constexpr auto JoinTypeLists(TypeList<T1s...>, TypeList<T2s...>)
    -> TypeList<T1s..., T2s...>;

template <typename List1, typename List2>
using JoinLists =
    decltype(JoinTypeLists(std::declval<List1>(), std::declval<List2>()));

/**
 * \brief Get I'th type in template type list.
 */
template <std::size_t I, typename T, typename... Ts>
struct TypeAtImpl {
  using type = typename TypeAtImpl<I - 1, Ts...>::type;
};

template <typename T, typename... Ts>
struct TypeAtImpl<0, T, Ts...> {
  using type = T;
};

template <std::size_t I, typename List>
struct TypeAt;

template <std::size_t I, typename... Ts>
struct TypeAt<I, TypeList<Ts...>> {
  using type = typename TypeAtImpl<I, Ts...>::type;
};

template <std::size_t I, typename TList>
using TypeAtT = typename TypeAt<I, TList>::type;

/**
 * \brief Get I'th argument in args list.
 */
template <std::size_t I, typename T, typename... TArgs>
constexpr auto&& ArgAt(T&& arg, TArgs&&... args) {
  if constexpr (I == 0) {
    return std::forward<T>(arg);
  } else {
    return ArgAt<I - 1>(std::forward<TArgs>(args)...);
  }
}

/**
 * \brief Pass type list as template parameters to template T
 */
template <template <typename...> typename T, typename TList>
struct TypeListToTemplate;

template <template <typename...> typename T, typename... Ts>
struct TypeListToTemplate<T, TypeList<Ts...>> {
  using type = T<Ts...>;
};

template <typename TList>
struct ReversTypeList;

template <typename T, typename... Ts>
struct ReversTypeList<TypeList<T, Ts...>> {
  using type =
      JoinLists<typename ReversTypeList<TypeList<Ts...>>::type, TypeList<T>>;
};

template <typename T>
struct ReversTypeList<TypeList<T>> {
  using type = TypeList<T>;
};

}  // namespace ae

#endif  // AETHER_TYPES_TYPE_LIST_H_
