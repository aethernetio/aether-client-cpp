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

#ifndef AETHER_FORMAT_FORMAT_IMPL_H_
#define AETHER_FORMAT_FORMAT_IMPL_H_

#include <tuple>
#include <array>
#include <string>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <algorithm>
#include <string_view>
#include <type_traits>

#include "aether/mstream.h"

#include "aether/format/formatter.h"

namespace ae {
namespace format_internal {
template <typename T, typename Enable = void>
struct IsStream : std::false_type {};

template <typename T>
struct IsStream<T, std::void_t<decltype(std::declval<T&>() << int{0})>>
    : std::true_type {};

template <typename TStream>
struct FormatWriter;

template <typename Ob>
struct FormatWriter<omstream<Ob>> {
  explicit FormatWriter(omstream<Ob>& stream) : ostr{&stream} {}

  void write(std::uint8_t const* data, std::size_t size) {
    ostr->write(data, size);
  }

  void write(std::string_view sv) { ostr->write(sv.data(), sv.size()); }

  auto& stream() const { return *ostr; }

  omstream<Ob>* ostr;
};
template <typename Ob>
FormatWriter(omstream<Ob>& stream) -> FormatWriter<omstream<Ob>>;

template <>
struct FormatWriter<std::ostream> {
  explicit FormatWriter(std::ostream& stream) : ostr{&stream} {}

  void write(std::uint8_t const* data, std::size_t size) {
    ostr->write(reinterpret_cast<char const*>(data),
                static_cast<std::streamsize>(size));
  }

  void write(std::string_view str) {
    ostr->write(str.data(), static_cast<std::streamsize>(str.size()));
  }

  auto& stream() const { return *ostr; }

  std::ostream* ostr;
};
FormatWriter(std::ostream& stream) -> FormatWriter<std::ostream>;

// list of args to format
template <typename... T>
struct FormatArgs {
  constexpr explicit FormatArgs(T const&... args) : arguments{&args...} {}

  template <std::size_t I, typename TFormatContext>
  constexpr void Format(TFormatContext& ctx) {
    using arg_type = std::decay_t<std::tuple_element_t<I, std::tuple<T...>>>;
    auto const& arg = *std::get<I>(arguments);
    Formatter<arg_type>{}.Format(arg, ctx);
  }

  std::tuple<T const*...> arguments;
};

struct FormatEntry {
  constexpr std::string_view before_format() const {
    if (entry_string.empty()) {
      return {};
    }
    return entry_string.substr(0, before_format_size);
  }
  constexpr std::string_view options() const {
    if (entry_string.empty()) {
      return {};
    }
    return entry_string.substr(options_offset, options_size);
  }

  std::string_view entry_string;
  std::uint16_t before_format_size;
  std::uint16_t options_offset;
  std::uint16_t options_size;
  std::uint8_t index = std::numeric_limits<std::uint8_t>::max();
};

struct FormatScheme {
  static constexpr std::size_t kCount = 10;

  constexpr FormatScheme(char const* format)
      : FormatScheme{std::string_view{format}} {}

  template <std::size_t Size>
  constexpr FormatScheme(char const (&format)[Size])
      : FormatScheme{std::string_view{format, Size}} {}

  FormatScheme(std::string const& format)
      : FormatScheme{std::string_view{format}} {}

  constexpr FormatScheme(std::string_view format) {
    std::uint8_t index = 0;

    while (!format.empty()) {
      auto format_begin = FormatBegin(format);
      if (format_begin == std::string_view::npos) {
        break;
      }
      auto format_end = format.find_first_of('}', format_begin);
      if (format_end == std::string_view::npos) {
        break;
      }
      auto index_end = format.find_first_of(':', format_begin);
      if (index_end > format_end) {
        index_end = format_begin;
      }

      format_entries[index] = FormatEntry{
          format.substr(0, format_end + 1),
          static_cast<std::uint16_t>(format_begin),
          static_cast<std::uint16_t>(index_end + 1),
          static_cast<std::uint16_t>(format_end - index_end - 1),
          index,
      };
      index += 1;
      format = format.substr(format_end + 1, format.size() - format_end - 1);
    }
    if (!format.empty()) {
      format_entries[index] = FormatEntry{
          format, static_cast<std::uint16_t>(format.size()), 0,
          0,      std::numeric_limits<std::uint8_t>::max(),
      };
    }
  }

  constexpr FormatScheme(FormatScheme const& format_string)
      : FormatScheme{format_string.format_entries} {}

  template <std::size_t Size>
  constexpr explicit FormatScheme(std::array<FormatEntry, Size> const& fe_arr) {
    std::copy_n(std::begin(fe_arr), std::min(kCount, Size),
                std::begin(format_entries));
  }

  static constexpr std::size_t FormatBegin(std::string_view const format) {
    std::size_t format_begin{};
    format_begin = format.find_first_of('{', format_begin);
    if ((format_begin != std::string_view::npos) &&
        (format_begin + 1) != format.size() &&
        (format[format_begin + 1] == '{')) {  // escaped '{'
      format_begin += 1;
    }
    return format_begin;
  }

  std::array<FormatEntry, kCount> format_entries{};
};

template <std::size_t I>
struct Index {
  static constexpr auto value = I;
};

template <typename Func, std::size_t... Is>
void DispatchImpl(std::size_t index, [[maybe_unused]] Func func,
                  std::index_sequence<Is...> const& /* seq */) {
  bool res = ((index == Is ? (func(Index<Is>{}), true) : false) || ...);
  (void)(res);
}

// Dispatch runtime index to compile time Index<I>
template <std::size_t Size, typename Func>
void Dispatch(std::size_t index, Func&& func) {
  DispatchImpl(index, std::forward<Func>(func),
               std::make_index_sequence<Size>{});
}

template <typename TStream, typename... T>
void FormatToStream(TStream& out, FormatScheme const& format_scheme,
                    FormatArgs<T...> args) {
  for (auto const& fmt : format_scheme.format_entries) {
    if (auto before = fmt.before_format(); !before.empty()) {
      out.write(before);
    }
    if (fmt.index < sizeof...(T)) {
      Dispatch<sizeof...(T)>(fmt.index, [&](auto index) {
        auto ctx = FormatContext{out, fmt.options()};
        args.template Format<decltype(index)::value>(ctx);
      });
    }
  }
}

}  // namespace format_internal

template <typename TStream, typename... Args>
void Format(format_internal::FormatWriter<TStream>& out_writer,
            format_internal::FormatScheme const& format, Args&&... args) {
  format_internal::FormatToStream(
      out_writer, format,
      format_internal::FormatArgs<std::decay_t<Args>...>{args...});
}

template <typename TStream, typename... Args>
std::enable_if_t<format_internal::IsStream<TStream>::value> Format(
    TStream& stream, format_internal::FormatScheme const& format,
    Args&&... args) {
  auto format_writer = format_internal::FormatWriter{stream};
  Format(format_writer, format, std::forward<Args>(args)...);
}

template <typename... Args>
std::string Format(format_internal::FormatScheme const& format,
                   Args&&... args) {
  auto stream = std::stringstream{};
  Format(stream, format, std::forward<Args>(args)...);
  return stream.str();
}
}  // namespace ae

#endif  // AETHER_FORMAT_FORMAT_IMPL_H_
