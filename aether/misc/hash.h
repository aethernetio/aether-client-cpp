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

#ifndef AEHTER_MISC_HASH_H_
#define AEHTER_MISC_HASH_H_

#include <type_traits>

#include "aether/crc.h"

namespace ae {
template <typename T, typename Enable = void>
struct Hasher;

/**
 * \brief Get combined hash of multiple values
 */
template <typename... TArgs>
constexpr crc32::result_t Hash(crc32::result_t res, TArgs const&... args) {
  ((res = Hasher<TArgs>::Get(args, res)), ...);
  return res;
}

template <typename... TArgs>
constexpr std::uint32_t Hash(TArgs const&... args) {
  crc32::result_t res{};
  res = Hash(res, args...);
  return res.value;
}

/**
 * Hasher implementations for various types.
 */

// For integral and floating-point types
template <typename T>
struct Hasher<
    T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>> {
  static constexpr crc32::result_t Get(T value, crc32::result_t res) {
    return crc32::from_buffer(reinterpret_cast<std::uint8_t*>(&value),
                              sizeof(T), res);
  }
};

// For enums
template <typename T>
struct Hasher<T, std::enable_if_t<std::is_enum_v<T>>> {
  static constexpr crc32::result_t Get(T value, crc32::result_t res) {
    return crc32::from_buffer(reinterpret_cast<std::uint8_t*>(&value),
                              sizeof(T), res);
  }
};

// For string values
template <typename T>
struct Hasher<T, std::enable_if_t<std::is_same_v<std::string, T>>> {
  static constexpr crc32::result_t Get(T const& value, crc32::result_t res) {
    return crc32::from_string(value.c_str(), res);
  }
};

template <typename T>
struct Hasher<T, std::enable_if_t<std::is_same_v<std::string_view, T>>> {
  static constexpr crc32::result_t Get(T const& value, crc32::result_t res) {
    return crc32::from_buffer(
        reinterpret_cast<std::uint8_t const*>(value.data()), value.size(), res);
  }
};

template <std::size_t S>
struct Hasher<char[S]> {
  static constexpr crc32::result_t Get(char const (&value)[S],
                                       crc32::result_t res) {
    return crc32::from_string(value, res);
  }
};

}  // namespace ae

#endif  // AEHTER_MISC_HASH_H_
