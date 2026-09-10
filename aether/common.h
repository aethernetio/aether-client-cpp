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

#include <cstdint>

#if ESP_PLATFORM
#  include "esp_attr.h"
#endif

#include "aether/config.h"

#define _QUOTE(x) #x
#define STR(x) _QUOTE(x)
#define VA_STR(...) #__VA_ARGS__

#define AE_CAT_(A, B) A##B
#define AE_CAT(A, B) AE_CAT_(A, B)
#define AE_UNIQUE_NAME(P) AE_CAT(P, AE_CAT(__LINE__, __COUNTER__))

// remove () around X
#define AE_DEPAREN(X) AE_ESC(AE_ISH X)
#define AE_ISH(...) AE_ISH __VA_ARGS__
#define AE_ESC(...) AE_ESC_(__VA_ARGS__)
#define AE_ESC_(...) AE_VAN_##__VA_ARGS__
#define AE_VAN_AE_ISH

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

#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN ||                 \
    defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
    defined(__AARCH64EB__) || defined(_MIBSEB) || defined(__MIBSEB) ||       \
    defined(__MIBSEB__) ||                                                   \
    defined(Q_BYTE_ORDER) && Q_BYTE_ORDER == Q_BIG_ENDIAN
#  define AE_ENDIANNESS AE_BIG_ENDIAN
#elif defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN ||          \
    defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) ||                    \
    defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(__i386__) || \
    defined(__amd64) || defined(__amd64__) || defined(_MIPSEL) ||          \
    defined(__MIPSEL) || defined(__MIPSEL__) || defined(ESP_PLATFORM) ||   \
    defined(Q_BYTE_ORDER) && Q_BYTE_ORDER == Q_LITTLE_ENDIAN
#  define AE_ENDIANNESS AE_LITTLE_ENDIAN
#else
#  define AE_ENDIANNESS AE_LITTLE_ENDIAN
// #error "Undefined endianness for the architecture"
#endif

namespace ae {
template <typename T>
T SwapToInet(const T& t) {
#if AE_ENDIANNESS == AE_LITTLE_ENDIAN
  union {
    T t;
    std::uint8_t t8[sizeof(T)];
  } src, dest;
  src.t = t;
  for (size_t e = 0; e < sizeof(T); e++) {
    dest.t8[e] = src.t8[sizeof(T) - e - 1];
  }
  return dest.t;
#else
  return t;
#endif
}

template <typename T>
T SwapToLittleEndian(const T& t) {
#if AE_ENDIANNESS == AE_BIG_ENDIAN
  union {
    T t;
    std::uint8_t t8[sizeof(T)];
  } src, dest;
  src.t = t;
  for (size_t e = 0; e < sizeof(T); e++) {
    dest.t8[e] = src.t8[sizeof(T) - e - 1];
  }
  return dest.t;
#else
  return t;
#endif
}
}  // namespace ae

#if ESP_PLATFORM
#  define RTC_STORAGE_ATTR RTC_DATA_ATTR
#else
#  define RTC_STORAGE_ATTR
#endif

#endif  // AETHER_COMMON_H_
