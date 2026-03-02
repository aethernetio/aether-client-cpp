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
#include <type_traits>

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

static inline constexpr std::size_t GetTypeAtChunkSize = 10;

template <std::size_t I, typename T0, typename T1 = void, typename T2 = void,
          typename T3 = void, typename T4 = void, typename T5 = void,
          typename T6 = void, typename T7 = void, typename T8 = void,
          typename T9 = void>
static constexpr auto GetTypeAtChunk() {
  static_assert(I < GetTypeAtChunkSize);
  if constexpr (I == 0) {
    return std::type_identity<T0>{};
  } else if constexpr (I == 1) {
    return std::type_identity<T1>{};
  } else if constexpr (I == 2) {
    return std::type_identity<T2>{};
  } else if constexpr (I == 3) {
    return std::type_identity<T3>{};
  } else if constexpr (I == 4) {
    return std::type_identity<T4>{};
  } else if constexpr (I == 5) {
    return std::type_identity<T5>{};
  } else if constexpr (I == 6) {
    return std::type_identity<T6>{};
  } else if constexpr (I == 7) {
    return std::type_identity<T7>{};
  } else if constexpr (I == 8) {
    return std::type_identity<T8>{};
  } else if constexpr (I == 9) {
    return std::type_identity<T9>{};
  }
}

template <std::size_t I, typename T0, typename T1 = void, typename T2 = void,
          typename T3 = void, typename T4 = void, typename T5 = void,
          typename T6 = void, typename T7 = void, typename T8 = void,
          typename T9 = void, typename... Ts>
static constexpr auto GetTypeAt() {
  if constexpr (I < GetTypeAtChunkSize) {
    return GetTypeAtChunk<I, T0, T1, T2, T3, T4, T5, T6, T7, T8, T9>();
  } else {
    return GetTypeAt<I - GetTypeAtChunkSize, Ts...>();
  }
}

template <std::size_t I, typename TList>
struct TypeAt;

template <std::size_t I, typename... Ts>
struct TypeAt<I, TypeList<Ts...>> {
  using type = typename decltype(GetTypeAt<I, Ts...>())::type;
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
