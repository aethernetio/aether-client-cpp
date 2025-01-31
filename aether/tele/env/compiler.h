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

#ifndef AETHER_TELE_ENV_COMPILER_H_
#define AETHER_TELE_ENV_COMPILER_H_

#include "aether/common.h"

#if defined __clang__
#  if defined __MINGW32__
#    define COMPILER "mingw clang"
#  else
#    define COMPILER "clang"
#  endif
#  define COMPILER_VERSION \
    STR(__clang_major__)   \
    "." STR(__clang_minor__) "." STR(__clang_patchlevel__)
#  define COMPILER_VERSION_NUM \
    AE_CONCAT(__clang_major__, __clang_minor__, __clang_patchlevel__)
#elif defined __GNUC__
#  if defined __MINGW32__
#    define COMPILER "mingw gcc"
#  else
#    define COMPILER "gcc"
#  endif
#  define COMPILER_VERSION \
    STR(__GNUC__) "." STR(__GNUC_MINOR__) "." STR(__GNUC_PATCHLEVEL__)
#  define COMPILER_VERSION_NUM \
    AE_CONCAT(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#elif defined _MSC_VER
#  define COMPILER "msvc"
#  define COMPILER_VERSION STR(_MSC_FULL_VER)
#  define COMPILER_VERSION_NUM _MSC_FULL_VER
#else
#  warning "unknown compiler"
#  define COMPILER "unknown"
#  if defined __VERSION__
#    define COMPILER_VERSION __VERSION__
#  else
#    define COMPILER_VERSION "unknown"
#    define COMPILER_VERSION_NUM 0
#  endif
#endif

#endif  // AETHER_TELE_ENV_COMPILER_H_ */
