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

#ifndef AETHER_META_ARG_AT_H_
#define AETHER_META_ARG_AT_H_

#include <cstddef>
#include <utility>

namespace ae {
/**
 * \brief Get I'th argument in args list.
 */
template <std::size_t I, typename T, typename... TArgs>
constexpr decltype(auto) VarAt([[maybe_unused]] T&& arg,
                               [[maybe_unused]] TArgs&&... args) {
  static_assert(I <= sizeof...(args), "Index out of bounds");
  if constexpr (I == 0) {
    return std::forward<T>(arg);
  } else {
    return VarAt<I - 1>(std::forward<TArgs>(args)...);
  }
}

template <std::size_t I, auto Arg, auto... Args>
struct ArgAt {
  static_assert(I <= sizeof...(Args), "Index out of bounds");
  static constexpr auto value = ArgAt<I - 1, Args...>::value;
};

template <auto Arg, auto... Args>
struct ArgAt<0, Arg, Args...> {
  static constexpr auto value = Arg;
};

template <std::size_t I, auto... Args>
static inline constexpr auto ArgAt_v = ArgAt<I, Args...>::value;

}  // namespace ae

#if AE_TESTS
#  include "tests/inline.h"

namespace test::arg_at_h {
AE_TEST_INLINE(test_VarAt) {
  constexpr int x = 42;
  constexpr double y = 3.14;
  constexpr bool z = true;

  static_assert(ae::VarAt<0>(x, y, z) == 42);
  static_assert(ae::VarAt<1>(x, y, z) == 3.14);
  static_assert(ae::VarAt<2>(x, y, z));

  int a = 10;
  float b = 3.14F;
  bool c = true;

  TEST_ASSERT_EQUAL(10, ae::VarAt<0>(a, b, c));
  TEST_ASSERT_EQUAL_FLOAT(3.14F, ae::VarAt<1>(a, b, c));
  TEST_ASSERT_EQUAL(true, ae::VarAt<2>(a, b, c));
}

template <auto... Args>
inline void testTemplateArgs() {
  TEST_ASSERT_EQUAL(10, (ae::ArgAt_v<0, Args...>));
  TEST_ASSERT_EQUAL_FLOAT(3.14F, (ae::ArgAt_v<1, Args...>));
  TEST_ASSERT_EQUAL(true, (ae::ArgAt_v<2, Args...>));
}

AE_TEST_INLINE(test_ArgAt) {
  constexpr int x = 42;
  constexpr double y = 3.14;
  constexpr bool z = true;

  static_assert(ae::ArgAt_v<0, x, y, z> == 42);
  static_assert(ae::ArgAt_v<1, x, y, z> == 3.14);
  static_assert(ae::ArgAt_v<2, x, y, z>);

  testTemplateArgs<10, 3.14F, true>();
}

}  // namespace test::arg_at_h
#endif

#endif  // AETHER_META_ARG_AT_H_
