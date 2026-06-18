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

#ifndef AETHER_FORMAT_DEFAULT_FORMATTERS_H_
#define AETHER_FORMAT_DEFAULT_FORMATTERS_H_

#include <memory>
#include <cassert>
#include <cstdint>
#include <optional>
#include <iostream>
#include <charconv>
#include <type_traits>
#include <string_view>

#include "aether/type_traits.h"
#include "aether/format/formatter.h"
#include "aether/meta/time_traits.h"
#include "aether/format/format_impl.h"

namespace ae {
using format_internal::FormatEntry;
using format_internal::FormatScheme;

template <typename T, typename _ = void>
struct IsStreamOutputSpecified : std::false_type {};

template <typename T>
struct IsStreamOutputSpecified<
    T, std::void_t<decltype(std::declval<std::ostream&>()
                            << std::declval<T const&>())>> : std::true_type {};

template <typename T, typename _ = void>
struct IstextSpecified : std::false_type {};

template <typename T>
struct IstextSpecified<T,
                       std::void_t<decltype(T::text(std::declval<T const&>()))>>
    : std::true_type {};

// for any with operator<< to std::ostream&
template <typename T>
struct Formatter<
    T, std::enable_if_t<IsStreamOutputSpecified<T>::value &&
                        !IstextSpecified<T>::value && !std::is_enum_v<T> &&
                        !IsTimePoint<T>::value && !IsDuration<T>::value>> {
  template <typename TStream>
  void Format(T const& value, FormatContext<TStream>& ctx) const {
    ctx.out().stream() << value;
  }
};

// for any with text method
template <typename T>
struct Formatter<T, std::enable_if_t<IstextSpecified<T>::value>> {
  template <typename TStream>
  void Format(T const& value, FormatContext<TStream>& ctx) const {
    ctx.out().write(T::text(value));
  }
};

// for any enum
template <typename T>
struct Formatter<T, std::enable_if_t<std::is_enum_v<T>>>
    : Formatter<std::uint64_t> {
  template <typename TStream>
  void Format(T const& value, FormatContext<TStream>& ctx) const {
    Formatter<std::uint64_t>::Format(static_cast<std::uint64_t>(value), ctx);
  }
};

// that can be iterated
template <typename T>
struct Formatter<T, std::enable_if_t<!(IsString<std::decay_t<T>>::value ||
                                       IsStringView<std::decay_t<T>>::value) &&
                                     IsContainer<std::decay_t<T>>::value>> {
  template <typename TStream>
  void Format(T const& value, FormatContext<TStream>& ctx) const {
    if constexpr (std::is_same_v<std::uint8_t,
                                 std::decay_t<typename T::value_type>> ||
                  std::is_same_v<std::int8_t,
                                 std::decay_t<typename T::value_type>>) {
      FormatBuffer(value, ctx);
    } else {
      FormatContainer(value, ctx);
    }
  }

  template <typename TStream>
  void FormatContainer(T const& value, FormatContext<TStream>& ctx) const {
    auto format = FormatScheme{std::array{
        FormatEntry{ctx.options, 0, 0,
                    static_cast<std::uint16_t>(ctx.options.size()), 0},
        FormatEntry{{", "}, 2, 0, 0, 1},
    }};
    auto format_last = FormatScheme{std::array{
        FormatEntry{ctx.options, 0, 0,
                    static_cast<std::uint16_t>(ctx.options.size()), 0},
    }};

    for (auto it = std::begin(value); it != std::end(value); ++it) {
      if (std::next(it) == std::end(value)) {
        ae::Format(ctx.out(), format_last, *it);
      } else {
        ae::Format(ctx.out(), format, *it);
      }
    }
  }

  template <typename TStream>
  void FormatBuffer(T const& value, FormatContext<TStream>& ctx) const {
    static_assert(sizeof(typename T::value_type) == 1,
                  "Print buffer only for one byte size values");

    constexpr std::size_t kLocalBuffSize = 128;
    constexpr std::size_t kTwoMinCharValue = 0x10;
    constexpr int kPrintBase = 16;
    std::size_t v_size = 2;  // 2 chars on byte
    std::size_t buff_size = v_size * value.size();

    std::array<char, kLocalBuffSize> local_buff;
    std::unique_ptr<char[]> alloc_buff;  // NOLINT(*avoid-c-arrays)

    char* buff;  // NOLINT(*init-variables)
    if (buff_size > local_buff.size()) {
      alloc_buff =
          std::make_unique<char[]>(buff_size);  // NOLINT(*avoid-c-arrays)
      buff = alloc_buff.get();
    } else {
      buff = local_buff.data();
    }
    std::size_t wp = 0;
    for (auto const& v : value) {
      // convert value with leading 0
      if (v < kTwoMinCharValue) {
        *(buff + wp) = '0';
        std::to_chars(buff + wp + 1, buff + wp + v_size, v, kPrintBase);
      } else {
        std::to_chars(buff + wp, buff + wp + v_size, v, kPrintBase);
      }
      wp += v_size;
    }
    ctx.out().stream().write(buff, static_cast<std::streamsize>(wp));
  }
};

// for std::optional
template <typename T>
struct Formatter<std::optional<T>> : Formatter<T> {
  template <typename TStream>
  void Format(std::optional<T> const& value,
              FormatContext<TStream>& ctx) const {
    if (!value) {
      static constexpr std::string_view null_str = "nullopt";
      ctx.out().stream().write(null_str.data(), null_str.size());
    } else {
      Formatter<T>::Format(value.value(), ctx);
    }
  }
};

}  // namespace ae

#endif  // AETHER_FORMAT_DEFAULT_FORMATTERS_H_
