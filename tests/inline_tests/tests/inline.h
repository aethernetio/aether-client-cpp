/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef TESTS_INLINE_H_
#define TESTS_INLINE_H_

#include <cstdint>

#include "unity.h"

#include "tests/crc.h"

template <std::uint32_t>
void setup_inline_test_function() {};

template <std::uint32_t>
void teardown_inline_test_function() {};

template <std::uint32_t>
void run_inline_test_function() {};

#define AE_SETUP_INLINE                   \
  template <>                             \
  inline void setup_inline_test_function< \
      ::inline_tests::crc32::checksum_from_literal(__FILE__)>() {}

#define AE_TEARDOWN_INLINE                   \
  template <>                                \
  inline void teardown_inline_test_function< \
      ::inline_tests::crc32::checksum_from_literal(__FILE__)>() {}

#define AE_TEST_INLINE                  \
  template <>                           \
  inline void run_inline_test_function< \
      ::inline_tests::crc32::checksum_from_literal(__FILE__)>()

#define TEST(Function) RUN_TEST(Function)

#endif  // TESTS_INLINE_H_
