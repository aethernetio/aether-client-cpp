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

#ifndef AETHER_COMMON_H_
#define AETHER_COMMON_H_

#include <chrono>
#include <cstdint>

#include "aether/config.h"
#include "aether/mstream.h"

namespace ae {

#define _QUOTE(x) #x
#define STR(x) _QUOTE(x)

#define _AE_CONCAT_0(A, B, ...) A##B
#define _AE_CONCAT_1(A, ...) _AE_CONCAT_0(A##__VA_ARGS__)
#define _AE_CONCAT_2(A, ...) _AE_CONCAT_1(A##__VA_ARGS__)
#define _AE_CONCAT_3(A, ...) _AE_CONCAT_2(A##__VA_ARGS__)
#define _AE_CONCAT_N(_3, _2, _1, _0, X, ...) _AE_CONCAT##X(_3, _2, _1, _0)
#define AE_CONCAT(...) _AE_CONCAT_N(__VA_ARGS__, _2, _1, _0)

// remove () around X
#define AE_DEPAREN(X) AE_ESC(AE_ISH X)
#define AE_ISH(...) AE_ISH __VA_ARGS__
#define AE_ESC(...) AE_ESC_(__VA_ARGS__)
#define AE_ESC_(...) AE_VAN_##__VA_ARGS__
#define AE_VAN_AE_ISH

using ServerId = std::uint16_t;

using Duration = std::chrono::duration<uint32_t, std::micro>;
using ClockType = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<ClockType>;
inline auto Now() { return TimePoint::clock::now(); }

template <typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, const Duration&) {
  // TODO: implement
  s << std::uint32_t{0};
  return s;
}
template <typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, Duration&) {
  std::uint32_t t;
  s >> t;
  return s;
}

enum class CompressionMethod : std::uint8_t {
  kNone = AE_NONE,
#if AE_COMPRESSION_ZLIB == 1
  kZlib = 1,
#endif  // AE_COMPRESSION_ZLIB == 1
#if AE_COMPRESSION_LZMA == 1
  kLzma = 2,
#endif  // AE_COMPRESSION_LZMA == 1
};

}  // namespace ae

// default copy move constructors
#define AE_CLASS_MOVE_(class_name, impl)          \
  class_name(class_name&& other) noexcept = impl; \
  class_name& operator=(class_name&& other) noexcept = impl;

#define AE_CLASS_COPY_(class_name, impl)      \
  class_name(class_name const& other) = impl; \
  class_name& operator=(class_name const& other) = impl;

#define AE_CLASS_DEFAULT_MOVE(class_name) AE_CLASS_MOVE_(class_name, default)
#define AE_CLASS_NO_MOVE(class_name) AE_CLASS_MOVE_(class_name, delete)

#define AE_CLASS_DEFAULT_COPY(class_name) AE_CLASS_COPY_(class_name, default)
#define AE_CLASS_NO_COPY(class_name) AE_CLASS_COPY_(class_name, delete)

#define AE_CLASS_MOVE_ONLY(class_name) \
  AE_CLASS_COPY_(class_name, delete)   \
  AE_CLASS_MOVE_(class_name, default)

#define AE_CLASS_COPY_MOVE(class_name) \
  AE_CLASS_COPY_(class_name, default)  \
  AE_CLASS_MOVE_(class_name, default)

#define AE_CLASS_NO_COPY_MOVE(class_name) \
  AE_CLASS_COPY_(class_name, delete)      \
  AE_CLASS_MOVE_(class_name, delete)

// concepts
#define AE_REQUIRERS(Condition) \
  std::enable_if_t<AE_DEPAREN(Condition)::value, int> = 0

#define AE_REQUIRERS_NOT(Condition) \
  std::enable_if_t<!AE_DEPAREN(Condition)::value, int> = 0

#define AE_REQUIRERS_BOOL(Condition) \
  std::enable_if_t<AE_DEPAREN(Condition), int> = 0

#endif  // AETHER_COMMON_H_
