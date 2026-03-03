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

#ifndef AETHER_META_AS_TYPE_H_
#define AETHER_META_AS_TYPE_H_

#include <utility>
#include <type_traits>

namespace ae {
// AS rvalue
template <typename T, typename U>
T&& as_type(U&& u)
  requires std::is_same_v<U&&, decltype(u)>
{
  return static_cast<T&&>(std::forward<U>(u));
}

// AS lvalue
template <typename T, typename U>
T& as_type(U& u) {
  return static_cast<T&>(u);
}

// AS const lvalue
template <typename T, typename U>
T const& as_type(U const& u) {
  return static_cast<T const&>(u);
}
}  // namespace ae

#ifdef AE_TESTS

#  include "tests/inline.h"

namespace test::as_type_h {
using ae::as_type;

struct Base {};
struct Derived : Base {};

inline void test_AsType() {
  Derived d;
  decltype(auto) b1 = as_type<Base>(d);
  static_assert(std::is_same_v<Base&, decltype(b1)>);

  decltype(auto) b2 = as_type<Base>(std::as_const(d));
  static_assert(std::is_same_v<Base const&, decltype(b2)>);

  decltype(auto) b3 = as_type<Base>(std::move(d));
  static_assert(std::is_same_v<Base&&, decltype(b3)>);

  TEST_PASS();
}
}  // namespace test::as_type_h

AE_TEST_INLINE { TEST(test::as_type_h::test_AsType); }

#endif
#endif  // AETHER_META_AS_TYPE_H_
