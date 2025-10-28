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

#ifndef AETHER_TYPES_UID_H_
#define AETHER_TYPES_UID_H_

#include <array>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <string_view>

#include "aether/type_traits.h"
#include "aether/types/span.h"
#include "aether/format/format.h"
#include "aether/reflect/reflect.h"
#include "aether/types/literal_array.h"

namespace ae {
struct UidString {
  static constexpr std::size_t kUidStringSize = 32 + 4;

  template <std::size_t Size>
  constexpr UidString(char const (&str)[Size])
      : valid{(Size - 1) == kUidStringSize && (str[8] == '-') &&
              (str[13] == '-') && (str[18] == '-') && (str[23] == '-')},
        data{str, Size - 1} {}

  constexpr UidString(std::string_view str)
      : valid{(str[str.size() - 1] == '\0' ? (str.size() - 1) == kUidStringSize
                                           : str.size() == kUidStringSize) &&
              (str[8] == '-') && (str[13] == '-') && (str[18] == '-') &&
              (str[23] == '-')},
        data{str} {}

  UidString(std::string const& str) : UidString{std::string_view{str}} {
    assert(valid);
  }

  constexpr explicit operator std::array<std::uint8_t, 16>() const {
    // parse string like "f81d4fae-7dec-11d0-a765-00a0c91e6bf6"
    return ConcatArrays(
        MakeArray<4>(data.substr(0, 8)), MakeArray<2>(data.substr(9, 4)),
        MakeArray<2>(data.substr(14, 4)), MakeArray<2>(data.substr(19, 4)),
        MakeArray<6>(data.substr(24, 12)));
  }

  bool valid = false;
  std::string_view data;
};

struct Uid {
  static constexpr std::size_t kSize = 16;
  /**
   * \brief Get UID from string.
   * The https://www.ietf.org/rfc/rfc4122.txt format recognized only.
   */
  static constexpr Uid FromString(UidString str) {
    if (!str.valid) {
      return {};
    }

    return Uid{static_cast<std::array<std::uint8_t, 16>>(str)};
  }

  explicit constexpr Uid(std::array<std::uint8_t, kSize> uid) : value{uid} {}

  explicit constexpr Uid(std::uint8_t const (&uid)[kSize]) : value{} {
    for (std::size_t i = 0; i < kSize; ++i) {
      value[i] = uid[i];
    }
  }

  Uid() = default;

  AE_REFLECT_MEMBERS(value)

  bool operator<(const Uid& rhs) const { return value < rhs.value; }
  bool operator==(const Uid& rhs) const { return value == rhs.value; }
  bool operator!=(const Uid& rhs) const { return value != rhs.value; }

  bool empty() const { return value == std::array<std::uint8_t, kSize>{}; }

  std::array<std::uint8_t, kSize> value;
};

template <>
struct Formatter<Uid> {
  template <typename TStream>
  void Format(Uid const& uid, FormatContext<TStream>& ctx) const {
    ae::Format(ctx.out(), "{}-{}-{}-{}-{}", Span{uid.value.data(), 4},
               Span{uid.value.data() + 4, 2}, Span{uid.value.data() + 6, 2},
               Span{uid.value.data() + 8, 2}, Span{uid.value.data() + 10, 6});
  }
};

}  // namespace ae

#endif  // AETHER_TYPES_UID_H_ */
