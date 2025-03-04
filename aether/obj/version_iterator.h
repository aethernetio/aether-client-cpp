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

#include <type_traits>
#include <cstdint>
#include <utility>
#include <array>

#include "aether/type_traits.h"

namespace ae {
// use max version for compilation time optimization
// increase max version count if required
#if defined(MAX_OBJECT_VERSION)
inline constexpr std::uint8_t MAX_VERSION = MAX_OBJECT_VERSION;
#else
inline constexpr std::uint8_t MAX_VERSION = 24;
#endif

// Helper version tag for versioned function overloading
template <std::uint8_t V, typename T = void>
struct Version;

template <std::uint8_t V>
struct Version<V, std::enable_if_t<(V <= MAX_VERSION)>> {
  static constexpr std::uint8_t value = V;
};

// Traits for check supported versioned functionality

struct Dnv;

template <typename T, std::uint8_t V, typename _ = void>
struct HasVersionedLoad : std::false_type {};

template <typename T, std::uint8_t V>
struct HasVersionedLoad<T, V,
                        std::void_t<decltype(std::declval<T>().Load(
                            Version<V>{}, std::declval<Dnv&>()))>>
    : std::true_type {};

template <typename T, std::uint8_t V, typename _ = void>
struct HasVersionedSave : std::false_type {};

template <typename T, std::uint8_t V>
struct HasVersionedSave<T, V,
                        std::void_t<decltype(std::declval<T>().Save(
                            Version<V>{}, std::declval<Dnv&>()))>>
    : std::true_type {};

/**
 * \brief Calculate min and max version supported by T and tested with
 * VersionTrait
 */
template <typename T,
          template <typename, std::uint8_t, typename...> typename VersionTrait>
struct VersionBounds {
  template <std::uint8_t... Vs>
  static constexpr std::pair<std::uint8_t, std::uint8_t> CalcVersionBounds(
      std::integer_sequence<std::uint8_t, Vs...> const&) {
    constexpr auto arr = std::array{VersionTrait<T, Vs>::value...};
    std::uint8_t min{};
    std::uint8_t max{};
    std::size_t i = 0;
    for (; i < arr.size(); ++i) {
      if (arr[i]) {
        break;
      }
    }
    min = static_cast<std::uint8_t>(i);

    for (; i < arr.size(); ++i) {
      if (!arr[i]) {
        break;
      }
    }
    max = static_cast<std::uint8_t>(i - 1);
    return {min, max};
  }

  static constexpr auto value = CalcVersionBounds(
      std::make_integer_sequence<std::uint8_t, MAX_VERSION + 1>());
};

/**
 * \brief Min and max versions supported by T to Load
 */
template <typename T>
using VersionedLoadMinMax = VersionBounds<T, HasVersionedLoad>;

/**
 * \brief Min and max versions supported by T to Save
 */
template <typename T>
using VersionedSaveMinMax = VersionBounds<T, HasVersionedSave>;

template <typename T>
struct HasAnyVersionedLoad {
  template <uint8_t... Vs>
  static constexpr bool TestAny(std::integer_sequence<uint8_t, Vs...> const&) {
    return (HasVersionedLoad<T, Vs>::value || ...);
  }

  static constexpr auto version_bounds = VersionedLoadMinMax<T>::value;
  static constexpr bool value =
      TestAny(make_range_sequence<std::uint8_t, version_bounds.first,
                                  version_bounds.second>());
};

template <typename T>
struct HasAnyVersionedSave {
  template <uint8_t... Vs>
  static constexpr bool TestAny(std::integer_sequence<uint8_t, Vs...> const&) {
    return (HasVersionedSave<T, Vs>::value || ...);
  }

  static constexpr auto version_bounds = VersionedSaveMinMax<T>::value;
  static constexpr bool value =
      TestAny(make_range_sequence<std::uint8_t, version_bounds.first,
                                  version_bounds.second>());
};

template <template <typename, std::uint8_t, typename...> typename VersionTrait,
          std::uint8_t Vs, typename T, typename TFunc>
constexpr void IterateVersionsImplApply(T& t, TFunc&& func) {
  if constexpr (VersionTrait<std::decay_t<T>, Vs>::value) {
    func(Version<Vs>{}, t);
  }
}

template <template <typename, std::uint8_t, typename...> typename VersionTrait,
          typename T, typename TFunc, std::uint8_t... Vs>
constexpr void IterateVersionsImpl(
    T& t, TFunc&& func, std::integer_sequence<std::uint8_t, Vs...> const&) {
  (IterateVersionsImplApply<VersionTrait, Vs>(t, std::forward<TFunc>(func)),
   ...);
}

/**
 * \brief Iterate for each version of the object for that VersionTrait returns
 * true
 * \tparam VersionTrait Trait to check if version is supported
 * \tparam version_min Minimum version to iterate from
 * \tparam version_max Maximum version to iterate to
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

template <typename Visitor>
struct VersionNodeVisitor {
  constexpr explicit VersionNodeVisitor(Visitor vis)
      : visitor{std::forward<Visitor>(vis)} {}

  template <typename... U>
  void operator()(U&&... vals) {
    (std::forward<Visitor>(visitor)(std::forward<U>(vals)), ...);
  }

  Visitor visitor;
};

}  // namespace ae
#endif  // AETHER_OBJ_VERSION_ITERATOR_H_
