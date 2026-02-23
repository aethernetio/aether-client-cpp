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

#ifndef AETHER_ENV_H_
#define AETHER_ENV_H_

#include <chrono>
#include <cstdint>
#include <array>

#if ESP_PLATFORM
#  include "esp_attr.h"
#endif

#include "aether/config.h"

#ifdef __GNUC__
#  define AE_PACK(T, DECL) T __attribute__((__packed__)) DECL
#elif defined(_MSC_VER)
#  define AE_PACK(T, DECL) __pragma(pack(push, 1)) T DECL __pragma(pack(pop))
#else
#  error "Packing macro must be specified"
#endif

#define ALIGNED_TYPE(t) t ALIGNED_(1)

namespace ae {

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

// To avoid compiler warning "Unused variable". Helps in Log(...) macros.
template <typename... Args>
inline void Unused(Args&&...) {}

#define AE_MSVC_BUG_FIX(x) x

#if ESP_PLATFORM
#  define RTC_STORAGE_ATTR RTC_DATA_ATTR
#else
#  define RTC_STORAGE_ATTR
#endif

}  // namespace ae

#endif  // AETHER_ENV_H_
