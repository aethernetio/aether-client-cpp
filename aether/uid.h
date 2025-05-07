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

#ifndef AETHER_UID_H_
#define AETHER_UID_H_

#include <array>
#include <cstdint>
#include <cstddef>

#include "aether/literal_array.h"
#include "aether/reflect/reflect.h"

namespace ae {
struct Uid {
  static constexpr std::size_t kSize = 16;
  static constexpr std::size_t kUidStringSize = 32 + 4;
  static const Uid kAether;

  /**
   * \brief Get UID from string.
   * The https://www.ietf.org/rfc/rfc4122.txt format recognized only.
   */
  static Uid FromString(std::string_view str);

  template <std::size_t Size>
  static constexpr Uid FromLit(char const (&str)[Size]) {
    static_assert((Size - 1) == kUidStringSize, "Wrong uid format");

    // parse string like "f81d4fae-7dec-11d0-a765-00a0c91e6bf6"
    return Uid{
        ConcatArrays(FillArray<8>(str, std::make_index_sequence<4>()),
                     FillArray<4>(str + 9, std::make_index_sequence<2>()),
                     FillArray<4>(str + 14, std::make_index_sequence<2>()),
                     FillArray<4>(str + 19, std::make_index_sequence<2>()),
                     FillArray<12>(str + 24, std::make_index_sequence<6>()))};
  }

  explicit constexpr Uid(std::array<std::uint8_t, kSize> uid) : value(uid) {}
  Uid() = default;

  AE_REFLECT_MEMBERS(value)

  bool operator<(const Uid& rhs) const { return value < rhs.value; }
  bool operator==(const Uid& rhs) const { return value == rhs.value; }
  bool operator!=(const Uid& rhs) const { return value != rhs.value; }

  std::array<std::uint8_t, kSize> value;
};

}  // namespace ae

#endif  // AETHER_UID_H_ */
