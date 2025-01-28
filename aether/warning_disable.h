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

#ifndef AETHER_WARNING_DISABLE_H_
#define AETHER_WARNING_DISABLE_H_

#if defined _MSC_VER
#  define DO_PRAGMA(x) __pragma(x)
#elif defined __GNUC__ || defined __clang__
#  define DO_PRAGMA(x) _Pragma(#x)
#endif

#if defined _MSC_VER
#  define DISABLE_WARNING_PUSH() DO_PRAGMA(warning(push))
#  define DISABLE_WARNING_POP() DO_PRAGMA(warning(pop))
#  define IGNORE_WARNING(x) DO_PRAGMA(warning(disable : x))

#  define IGNORE_IMPLICIT_CONVERSION()
#  define IGNORE_WNAN_INFINITY_DISABLED()

#  define IGNORE_DEPRECATED() IGNORE_WARNING(4996)

#endif

#if defined __GNUC__ || defined __clang__
#  define DISABLE_WARNING_PUSH() DO_PRAGMA(GCC diagnostic push)
#  define DISABLE_WARNING_POP() DO_PRAGMA(GCC diagnostic pop)
#  define IGNORE_WARNING(name) DO_PRAGMA(GCC diagnostic ignored name)
#  define ADD_WARNING(name) DO_PRAGMA(GCC diagnostic warning name)

#  if defined __clang__
#    define IGNORE_IMPLICIT_CONVERSION()                \
      IGNORE_WARNING("-Wimplicit-int-conversion")       \
      IGNORE_WARNING("-Wimplicit-int-float-conversion") \
      IGNORE_WARNING("-Wsign-conversion")
#    if __clang_major__ >= 18
#      define IGNORE_WNAN_INFINITY_DISABLED() \
        IGNORE_WARNING("-Wnan-infinity-disabled")
#    else
#      define IGNORE_WNAN_INFINITY_DISABLED()
#    endif
#  else
#    define IGNORE_IMPLICIT_CONVERSION()  \
      IGNORE_WARNING("-Wsign-conversion") \
      IGNORE_WARNING("-Wconversion")

#    define IGNORE_WNAN_INFINITY_DISABLED()
#  endif
#  define IGNORE_DEPRECATED() IGNORE_WARNING("-Wdeprecated")
#endif

#if defined _MSC_VER
#  define AE_MAY_UNUSED_MEMBER [[maybe_unused]]
#endif

#if defined __clang__
#  define AE_MAY_UNUSED_MEMBER [[maybe_unused]]
#elif defined __GNUC__
// GCC does not check for unused private members.
#  define AE_MAY_UNUSED_MEMBER
#endif
#endif  // AETHER_WARNING_DISABLE_H_ */
