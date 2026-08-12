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

#ifndef AETHER_OBJ_VERSION_ITERATOR_H_
#define AETHER_OBJ_VERSION_ITERATOR_H_

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "aether-miscpp/meta/index_sequence.h"

namespace ae {
using VersionValueType = std::uint8_t;

// use max version for compilation time optimization
// increase max version count if required
#ifdef MAX_OBJECT_VERSION
inline constexpr VersionValueType kMaxVersion = MAX_OBJECT_VERSION;
#else
inline constexpr VersionValueType kMaxVersion = 24;
#endif

// Helper version tag for versioned function overloading
template <VersionValueType V>
  requires(V <= kMaxVersion)
struct Version : public std::integral_constant<VersionValueType, V> {};

// Traits for check supported versioned functionality

struct Dnv;

template <typename T, VersionValueType V>
struct VersionLoadTrait {
  template <typename U, typename _ = void>
  struct Has : std::false_type {};
  template <typename U>
  struct Has<U, std::void_t<decltype(std::declval<U&>().Load(
                    Version<V>{}, std::declval<Dnv&>()))>> : std::true_type {};

  static constexpr bool value = Has<T>::value;
};

template <typename T, VersionValueType V>
struct VersionSaveTrait {
  template <typename U, typename _ = void>
  struct Has : std::false_type {};
  template <typename U>
  struct Has<U, std::void_t<decltype(std::declval<U const&>().Save(
                    Version<V>{}, std::declval<Dnv&>()))>> : std::true_type {};

  static constexpr bool value = Has<T>::value;
};

/**
 * \brief Calculate min and max version supported by T and tested with
 * VersionTrait
 */
template <typename T,
          template <typename, VersionValueType> typename VersionTrait>
struct VersionBounds {
  template <VersionValueType... Vs>
  using i_seq = std::integer_sequence<VersionValueType, Vs...>;

  template <VersionValueType... Vs>
  static consteval auto CalcVersionBounds(i_seq<Vs...>)
      -> std::pair<VersionValueType, VersionValueType> {
    constexpr auto arr = std::array{VersionTrait<T, Vs>::value...};
    VersionValueType min{};
    VersionValueType max{};
    std::size_t i = 0;
    for (; i < arr.size(); ++i) {
      if (arr[i]) {
        break;
      }
    }
    if (i == arr.size()) {
      return {0, 0};
    }

    min = static_cast<VersionValueType>(i);

    for (; i < arr.size(); ++i) {
      if (!arr[i]) {
        break;
      }
    }
    max = static_cast<VersionValueType>(i - 1);
    return {min, max};
  }

  static constexpr auto value = CalcVersionBounds(
      std::make_integer_sequence<VersionValueType, kMaxVersion + 1>());
};

template <typename T,
          template <typename, VersionValueType> typename VersionTrait>
struct HasAnyVersioned {
  template <VersionValueType... Vs>
  static consteval bool TestAny(
      std::integer_sequence<VersionValueType, Vs...> const&) {
    return (VersionTrait<T, Vs>::value || ...);
  }

  static constexpr auto version_bounds = VersionBounds<T, VersionTrait>::value;
  static constexpr bool value =
      TestAny(make_range_sequence<std::uint8_t, version_bounds.first,
                                  version_bounds.second>());
};

template <typename T>
struct HasAnyVersionedLoad : public HasAnyVersioned<T, VersionLoadTrait> {};
template <typename T>
static constexpr inline bool HasAnyVersionedLoad_v =
    HasAnyVersionedLoad<T>::value;

template <typename T>
struct HasAnyVersionedSave : public HasAnyVersioned<T, VersionSaveTrait> {};
template <typename T>
static constexpr inline bool HasAnyVersionedSave_v =
    HasAnyVersionedSave<T>::value;

/**
 * \brief Iterate for each version of the object for that VersionTrait returns
 * true
 * \tparam VersionTrait Trait to check if version is supported
 * \tparam T Object type
 * \tparam TFunc Function to call for each version with signature
 * void(Version<V>, T&)
 * \param t Object to iterate
 * \param func Function to call for each version
 */
template <template <typename, std::uint8_t, typename...> typename VersionTrait,
          std::uint8_t version_min, std::uint8_t version_max, typename T,
          typename TFunc>
constexpr void IterateVersions(T& t, TFunc&& func) {
  IterateVersionsImpl<VersionTrait>(
      t, std::forward<TFunc>(func),
      make_range_sequence<std::uint8_t, version_min, version_max>());
}

template <template <typename, VersionValueType> typename VersionTrait>
struct VersionIterator {
 private:
  template <typename T, typename TFunc, VersionValueType V>
  static void ImplApply(T& t, TFunc&& func, Version<V> v) {
    if constexpr (VersionTrait<T, V>::value) {
      std::forward<TFunc>(func)(v, t);
    }
  }

  template <typename T, typename TFunc, VersionValueType... Vs>
  static void Impl(T& t, TFunc&& func,
                   std::integer_sequence<VersionValueType, Vs...>) {
    (ImplApply(t, std::forward<TFunc>(func), Version<Vs>{}), ...);
  }

 public:
  template <typename T, typename TFunc>
  constexpr void operator()(T& t, TFunc&& func) const {
    constexpr auto bounds = VersionBounds<T, VersionTrait>::value;
    Impl(t, std::forward<TFunc>(func),
         make_range_sequence<VersionValueType, bounds.first, bounds.second>());
  }
};

template <template <typename, VersionValueType> typename VersionTrait>
static constexpr inline auto version_iterator = VersionIterator<VersionTrait>{};

template <typename Visitor>
struct VersionNodeVisitor {
  template <typename U>
    requires(!std::is_same_v<std::decay_t<U>, VersionNodeVisitor>)
  constexpr explicit VersionNodeVisitor(U&& vis)
      : visitor{std::forward<U>(vis)} {}

  template <typename... U>
  void operator()(U&&... vals) {
    (visitor(std::forward<U>(vals)), ...);
  }

  Visitor visitor;
};

template <typename V>
VersionNodeVisitor(V&&) -> VersionNodeVisitor<V>;

}  // namespace ae
#endif  // AETHER_OBJ_VERSION_ITERATOR_H_
