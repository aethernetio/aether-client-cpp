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

#ifndef AETHER_TYPE_TRAITS_H_
#define AETHER_TYPE_TRAITS_H_

#include <type_traits>
#include <string>
#include <array>

namespace ae {

namespace _internal {
template <typename T, T... N, std::size_t... Indices>
constexpr auto reverse_sequence_helper(std::integer_sequence<T, N...>,
                                       std::index_sequence<Indices...>) {
  constexpr auto array = std::array<T, sizeof...(N)>{N...};
  return std::integer_sequence<T, array[sizeof...(Indices) - Indices - 1]...>();
}

template <typename T, T min, T... N>
constexpr auto make_range_sequence_helper(std::integer_sequence<T, N...>) {
  return std::integer_sequence<T, (N + min)...>();
}
}  // namespace _internal

template <typename T, T... N>
constexpr auto reverse_sequence(std::integer_sequence<T, N...> sequence) {
  return _internal::reverse_sequence_helper(
      sequence, std::make_index_sequence<sizeof...(N)>());
}

template <typename T, T from, T to>
constexpr auto make_range_sequence() {
  if constexpr (from <= to) {
    return _internal::make_range_sequence_helper<T, from>(
        std::make_integer_sequence<T, to - from + 1>());
  } else {
    // make reverse sequence from bigger to lesser
    return reverse_sequence(_internal::make_range_sequence_helper<T, to>(
        std::make_integer_sequence<T, from - to + 1>()));
  }
}

template <typename T>
struct IsString : std::false_type {};

template <>
struct IsString<std::string> : std::true_type {};

template <typename T>
struct IsStringView : std::false_type {};

template <>
struct IsStringView<std::string_view> : std::true_type {};

template <typename T, typename _ = void>
struct IsContainer : std::false_type {};

template <typename T>
struct IsContainer<
    T, std::conditional_t<
           false,
           std::void_t<typename T::value_type, typename T::size_type,
                       typename T::iterator, typename T::const_iterator,
                       decltype(std::declval<T>().size()),
                       decltype(std::declval<T>().begin()),
                       decltype(std::declval<T>().end()),
                       decltype(std::declval<T>().cbegin()),
                       decltype(std::declval<T>().cend())>,
           void>> : public std::true_type {};

template <typename T, typename _ = void>
struct IsAssociatedContainer : std::false_type {};

template <typename T>
struct IsAssociatedContainer<
    T, std::conditional_t<
           false,
           std::void_t<typename T::key_type, typename T::mapped_type,
                       typename T::value_type, typename T::size_type,
                       typename T::iterator, typename T::const_iterator,
                       decltype(std::declval<T>().size()),
                       decltype(std::declval<T>().begin()),
                       decltype(std::declval<T>().end()),
                       decltype(std::declval<T>().cbegin()),
                       decltype(std::declval<T>().cend())>,
           void>> : public std::true_type {};

template <typename T1, typename T2>
struct IsAbleToCast {
  static constexpr bool value =
      std::is_base_of_v<T1, T2> || std::is_base_of_v<T2, T1>;
};

}  // namespace ae

#endif  // AETHER_TYPE_TRAITS_H_ */