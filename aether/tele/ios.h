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

#ifndef AETHER_TELE_IOS_H_
#define AETHER_TELE_IOS_H_

#include <ios>
#include <string>
#include <utility>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <cassert>
#include <type_traits>

#include "aether/common.h"
#include "aether/uid.h"
#include "aether/type_traits.h"

namespace ae {
template <typename T, typename _ = void>
struct PrintToStream;

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

template <typename T>
struct PrintToStream<
    T, std::enable_if_t<IsStreamOutputSpecified<T>::value &&
                        !std::is_enum_v<T> && !IstextSpecified<T>::value>> {
  static void Print(std::ostream& s, T const& t) { s << t; }
};

static constexpr auto bi = IsStreamOutputSpecified<int>::value;

// that can be iterated
template <typename T>
struct PrintToStream<T,
                     std::enable_if_t<!(IsString<std::decay_t<T>>::value ||
                                        IsStringView<std::decay_t<T>>::value) &&
                                      IsContainer<std::decay_t<T>>::value>> {
  static void Print(std::ostream& s, T const& t) {
    s << "[";
    if constexpr (std::is_integral_v<typename T::value_type>) {
      for (auto it = std::begin(t); it != std::end(t); ++it) {
        s << std::setfill('0') << std::setw(2) << std::hex;
        if constexpr (std::is_unsigned_v<typename T::value_type>) {
          s << std::uint64_t{*it};
        } else {
          s << std::int64_t{*it};
        }
      }
      s << std::setfill(' ') << std::setw(0) << std::dec;
    } else {
      for (auto it = std::begin(t); it != std::end(t); ++it) {
        PrintToStream<typename T::value_type>::Print(s, *it);
      }
    }
    s << "]";
  }
};

// something that can converted to text
template <typename T>
struct PrintToStream<T, std::enable_if_t<IstextSpecified<T>::value>> {
  static void Print(std::ostream& s, T const& t) { s << T::text(t); }
};

template <typename T>
struct PrintToStream<T, std::enable_if_t<std::is_enum_v<T>>> {
  static void Print(std::ostream& s, T const& t) {
    s << static_cast<std::uint64_t>(t);
  }
};

template <>
struct PrintToStream<Uid> {
  static void Print(std::ostream& s, Uid const& t) {
    PrintToStream<decltype(Uid::value)>::Print(s, t.value);
  }
};

template <>
struct PrintToStream<TimePoint> {
  static void Print(std::ostream& s, TimePoint const& t) {
    // TODO: UTC time
    s << t.time_since_epoch().count();
  }
};

template <typename Iter>
void FormatInternal(std::ostream& stream, Iter& i, Iter const& end) {
  if (i != end) {
    stream.write(&*i, static_cast<std::streamsize>(std::distance(i, end)));
  }
}

template <typename Iter, typename Arg, typename... Args>
void FormatInternal(std::ostream& stream, Iter& i, Iter const& end, Arg&& arg,
                    Args&&... args) {
  auto const start = i;

  for (; i != end; ++i) {
    if (*i == '{') {
      stream.write(&*start,
                   static_cast<std::streamsize>(std::distance(start, i)));
      ++i;
      ++i;  // eat the '}'
      PrintToStream<std::decay_t<Arg>>::Print(stream, arg);
      FormatInternal(stream, i, end, std::forward<Args>(args)...);
      return;
    }
  }
  if (start != i) {
    stream.write(&*start,
                 static_cast<std::streamsize>(std::distance(start, i)));
  }
}

template <typename... Args>
void Format(std::ostream& stream, const std::string& format, Args&&... args) {
  if constexpr (sizeof...(args) == 0) {
    stream << format;
  } else {
    auto i = std::begin(format);
    auto end = std::end(format);
    FormatInternal(stream, i, end, std::forward<Args>(args)...);
  }
}

template <typename... Args>
std::string Format(const std::string& format, Args&&... args) {
  if constexpr (sizeof...(args) == 0) {
    return format;
  } else {
    std::stringstream stream;
    Format(stream, format, std::forward<Args>(args)...);
    return stream.str();
  }
}
}  // namespace ae

#endif  // AETHER_TELE_IOS_H_ */
