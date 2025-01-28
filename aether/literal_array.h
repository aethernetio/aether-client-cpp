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
#include <vector>
#include <cstdint>
#include <utility>
#include <string_view>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_WNAN_INFINITY_DISABLED()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/gcem/include/gcem.hpp"
DISABLE_WARNING_POP()

namespace ae {

template <std::size_t N>
struct LiteralArray {
  std::array<std::uint8_t, N> value_{};

  constexpr LiteralArray(std::array<std::uint8_t, N> value)
      : value_{std::move(value)} {}

  constexpr operator std::array<std::uint8_t, N>() const { return value_; }
  operator std::vector<std::uint8_t>() const {
    return {value_.begin(), value_.end()};
  }
};

namespace _internal {
static constexpr auto HexToDec(const char* str, std::size_t size) {
  constexpr auto base = 16;

  std::uint64_t result = 0;
  for (std::size_t i = 0; i < size; ++i) {
    if (str[i] == '\0') {
      break;
    }
    if ((str[i] >= '0') && (str[i] <= '9')) {
      result = result * base + static_cast<uint64_t>(str[i] - '0');
    }
    if ((str[i] >= 'a') && (str[i] <= 'f')) {
      result = result * base + static_cast<uint64_t>(str[i] - 'a' + 10);
    }
    if ((str[i] >= 'A') && (str[i] <= 'F')) {
      result = result * base + static_cast<uint64_t>(str[i] - 'A' + 10);
    }
  }
  return result;
}

template <std::size_t StrSize, std::size_t Index>
static constexpr auto StringIterator(const char (&str)[StrSize]) {
  constexpr auto str_index = Index * 2;
  return static_cast<std::uint8_t>(HexToDec(&str[str_index], 2));
}

template <std::size_t StrSize, std::size_t... Is>
static constexpr auto FillArray(const char (&str)[StrSize],
                                std::index_sequence<Is...> const&) {
  return std::array{StringIterator<StrSize, Is>(str)...};
}
}  // namespace _internal

template <std::size_t Size>
constexpr auto MakeLiteralArray(const char (&str)[Size]) {
  constexpr auto N =
      static_cast<std::size_t>(gcem::ceil(static_cast<float>(Size - 1) / 2));
  return LiteralArray<N>{
      _internal::FillArray<Size>(str, std::make_index_sequence<N>{})};
}

inline auto MakeArray(std::string_view str) {
  std::vector<std::uint8_t> result;
  for (std::size_t i = 0; i < str.size(); i += 2) {
    auto hex_str = str.substr(i, 2);
    auto value = _internal::HexToDec(hex_str.data(), 2);
    result.push_back(static_cast<std::uint8_t>(value));
  }
  return result;
}

}  // namespace ae

#endif  // AETHER_LITERAL_ARRAY_H_ */
