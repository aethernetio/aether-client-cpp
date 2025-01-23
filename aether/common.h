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

#define AE_CLASS_MOVE_(class_name, impl)          \
  class_name(class_name&& other) noexcept = impl; \
  class_name& operator=(class_name&& other) noexcept = impl;

#define AE_CLASS_COPY_(class_name, impl)      \
  class_name(class_name const& other) = impl; \
  class_name& operator=(class_name const& other) = impl;

#define AE_CLASS_MOVE_DEFAULT(class_name) AE_CLASS_MOVE_(class_name, default)
#define AE_CLASS_MOVE_DELETE(class_name) AE_CLASS_MOVE_(class_name, delete)

#define AE_CLASS_COPY_DEFAULT(class_name) AE_CLASS_COPY_(class_name, default)
#define AE_CLASS_COPY_DELETE(class_name) AE_CLASS_COPY_(class_name, delete)

#define AE_CLASS_MOVE_ONLY(class_name) \
  AE_CLASS_COPY_(class_name, delete)   \
  AE_CLASS_MOVE_(class_name, default)

#define AE_CLASS_COPY_MOVE(class_name) \
  AE_CLASS_COPY_(class_name, default)  \
  AE_CLASS_MOVE_(class_name, default)

#define AE_CLASS_NO_COPY_MOVE(class_name) \
  AE_CLASS_COPY_(class_name, delete)      \
  AE_CLASS_MOVE_(class_name, delete)

#endif  // AETHER_COMMON_H_
