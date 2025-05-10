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

#ifndef AETHER_LITERAL_ARRAY_H_
#define AETHER_LITERAL_ARRAY_H_

#include <array>
#include <cstdint>
#include <string_view>

#include "aether/common.h"

namespace ae {
namespace _internal {
static constexpr std::uint8_t FromHex(char ch) {
  constexpr std::uint8_t dec_max = 10;
  if ((ch >= '0') && (ch <= '9')) {
    return static_cast<std::uint8_t>(ch - '0');
  }
  if ((ch >= 'a') && (ch <= 'f')) {
    return static_cast<std::uint8_t>(ch - 'a' + dec_max);
  }
  if ((ch >= 'A') && (ch <= 'F')) {
    return static_cast<std::uint8_t>(ch - 'A' + dec_max);
  }
  return std::uint8_t{};
}
}  // namespace _internal

template <std::size_t N>
static constexpr auto MakeArray(std::string_view str) {
  std::array<std::uint8_t, N> result{};
  constexpr std::uint8_t base = 16;
  std::size_t index = 0;
  for (std::size_t i = 0; i < str.size();) {
    std::uint8_t value = _internal::FromHex(str[i++]);
    if (i < str.size()) {
      value = static_cast<std::uint8_t>(value * base) +
              _internal::FromHex(str[i++]);
    }
    result[index++] = value;
  }
  return result;
}

template <std::size_t Size>
static constexpr auto MakeArray(char const (&str)[Size]) {
  constexpr std::size_t N = Size / 2;
  return MakeArray<N>(std::string_view{str, Size - 1});
}

}  // namespace ae

#endif  // AETHER_LITERAL_ARRAY_H_ */
