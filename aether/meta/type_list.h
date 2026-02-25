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

namespace ae {
template <typename... T>
struct TypeList {
  TypeList() = default;
  template <typename... U>
  explicit constexpr TypeList(U&&...) {}
};

template <typename... T>
TypeList(T...) -> TypeList<T...>;

template <std::size_t I, typename TList>
struct TypeAt;

template <std::size_t I, typename T, typename... Ts>
struct TypeAt<I, TypeList<T, Ts...>> {
  using type = typename TypeAt<I - 1, TypeList<Ts...>>::type;
};

template <typename T, typename... Ts>
struct TypeAt<0, TypeList<T, Ts...>> {
  using type = T;
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

template <typename T, typename... Ts>
struct ReversTypeList<TypeList<T, Ts...>> {
  using type = JoinedTypeList_t<typename ReversTypeList<TypeList<Ts...>>::type,
                                TypeList<T>>;
};

template <typename T>
struct ReversTypeList<TypeList<T>> {
  using type = TypeList<T>;
};

}  // namespace ae
#endif  // AETHER_META_TYPE_LIST_H_
