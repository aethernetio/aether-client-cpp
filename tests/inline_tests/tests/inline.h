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

#include <array>
#include <cstdint>
#include <source_location>
#include <string_view>

#include "unity.h"

#include "tests/crc.h"

namespace inline_tests {
struct TestFunction {
  std::string_view func_name;
  int file_line;
  void (*test_func)();
};

struct TestRegistry {
  std::array<TestFunction, 10> test_functions;
  std::size_t next = 0;
};

[[maybe_unused]] static inline TestRegistry test_registry{};

static inline auto add_test([[maybe_unused]] std::string_view name,
                            [[maybe_unused]] void (*test_func)(),
                            [[maybe_unused]] std::source_location sl =
                                std::source_location::current()) {
#ifdef TEST_FILE
  printf("source file  %s test file %s\n", sl.file_name(), TEST_FILE);
  if (std::string_view{sl.file_name()} == TEST_FILE) {
    TestFunction tf{name, static_cast<int>(sl.line()), test_func};
    test_registry.test_functions[test_registry.next++] = tf;
  }
#endif
  return true;
}
}  // namespace inline_tests

#define _AE_CAT_IMPL(a, b) a##b
#define _AE_CAT(a, b) _AE_CAT_IMPL(a, b)

#ifdef TEST_FILE
#  define AE_TEST_INLINE(NAME)                                 \
    static void NAME();                                        \
    static inline const auto _AE_CAT(__add_test__, __LINE__) = \
        ::inline_tests ::add_test(#NAME, NAME);                \
    void NAME()
#else
#  define AE_TEST_INLINE(NAME) static inline void NAME()
#endif
#endif  // TESTS_INLINE_H_
