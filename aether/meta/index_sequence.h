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

#ifndef AETHER_META_INDEX_SEQUENCE_H_
#define AETHER_META_INDEX_SEQUENCE_H_

#include <array>
#include <cstddef>
#include <utility>

namespace ae {
namespace index_sequence_internal {
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
}  // namespace index_sequence_internal

template <typename T, T... N>
constexpr auto reverse_sequence(std::integer_sequence<T, N...> sequence) {
  return index_sequence_internal::reverse_sequence_helper(
      sequence, std::make_index_sequence<sizeof...(N)>());
}

template <typename T, T from, T to>
constexpr auto make_range_sequence() {
  if constexpr (from <= to) {
    return index_sequence_internal::make_range_sequence_helper<T, from>(
        std::make_integer_sequence<T, to - from + 1>());
  } else {
    // make reverse sequence from bigger to lesser
    return reverse_sequence(
        index_sequence_internal::make_range_sequence_helper<T, to>(
            std::make_integer_sequence<T, from - to + 1>()));
  }
}

}  // namespace ae

#if AE_TESTS
#  include "tests/inline.h"

namespace tests::index_sequence_h {
AE_TEST_INLINE(test_ReverseSequence) {
  constexpr auto rev_indices =
      ae::reverse_sequence(std::make_index_sequence<10>());
  static_assert(
      std::is_same_v<std::index_sequence<9, 8, 7, 6, 5, 4, 3, 2, 1, 0>,
                     std::decay_t<decltype(rev_indices)>>);

  constexpr auto types_rev_indices =
      ae::reverse_sequence(std::index_sequence_for<int, float, bool, double>());
  static_assert(std::is_same_v<std::index_sequence<3, 2, 1, 0>,
                               std::decay_t<decltype(types_rev_indices)>>);
  TEST_PASS();
}
}  // namespace tests::index_sequence_h
#endif

#endif  // AETHER_META_INDEX_SEQUENCE_H_
