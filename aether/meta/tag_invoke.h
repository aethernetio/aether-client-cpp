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

#ifndef AETHER_META_TAG_INVOKE_H_
#define AETHER_META_TAG_INVOKE_H_

#include <utility>

namespace ae {

namespace _tag_invoke {
void tag_invoke();

struct fn_ {
  template <typename Cpo, typename... Args>
  constexpr auto operator()(Cpo&& cpo, Args&&... args) const
      noexcept(noexcept(tag_invoke(std::forward<Cpo>(cpo),
                                   std::forward<Args>(args)...)))
          -> decltype(tag_invoke(std::forward<Cpo>(cpo),
                                 std::forward<Args>(args)...)) {
    return tag_invoke(std::forward<Cpo>(cpo), std::forward<Args>(args)...);
  }
};

template <typename Cpo, typename... Args>
using tag_invoke_result_t =
    decltype(tag_invoke(std::declval<Cpo&&>(), std::declval<Args&&>()...));

template <typename Cpo, typename... Args,
          typename = tag_invoke_result_t<Cpo, Args...>>
std::true_type TestTagInvocable(int) noexcept(
    noexcept(tag_invoke(std::declval<Cpo&&>(), std::declval<Args&&>()...)));

template <typename Cpo, typename... Args>
std::false_type TestTagInvocable(...);

}  // namespace _tag_invoke
inline constexpr auto tag_invoke = _tag_invoke::fn_{};

using _tag_invoke::tag_invoke_result_t;
using _tag_invoke::TestTagInvocable;

template <typename Cpo, typename... Args>
inline constexpr bool TagInvocable_v =
    decltype(TestTagInvocable<Cpo, Args...>(0))::value;

template <typename T, typename Cpo, typename... Args>
concept TagInvocable =
    TagInvocable_v<Cpo, T, Args...> || TagInvocable_v<Cpo, T&, Args...> ||
    TagInvocable_v<Cpo, T const&, Args...> || TagInvocable_v<Cpo, T&&, Args...>;

/**
 * \brief Helper struct to define cpo tag
 */
template <auto Cpo>
using tag_t = std::decay_t<decltype(Cpo)>;

}  // namespace ae

#ifdef AE_TESTS
#  include "tests/inline.h"

namespace test::tag_invoke_h {
inline constexpr struct TestCpo {
  void operator()(ae::TagInvocable<TestCpo> auto& t) const {
    ae::tag_invoke(*this, t);
  }
} test_cpo{};

inline constexpr struct TestGetInt {
  int operator()(ae::TagInvocable<TestGetInt> auto& t) const {
    return ae::tag_invoke(*this, t);
  }
} test_get_int{};

struct Foo {
  bool& invoked;

  friend void tag_invoke(ae::tag_t<test_cpo>, Foo& foo) noexcept {
    foo.invoked = true;
  }
};

struct Bar {
  int value;

  friend int tag_invoke(ae::tag_t<test_get_int>, Bar const& bar) noexcept {
    return bar.value;
  }
};

AE_TEST_INLINE(test_TagInvoke) {
  using TestCpoType = ae::tag_t<test_cpo>;
  static_assert(std::is_same_v<TestCpo, TestCpoType>);
  using TestGetIntType = ae::tag_t<test_get_int>;
  static_assert(std::is_same_v<TestGetInt, TestGetIntType>);

  using foo_res = ae::tag_invoke_result_t<ae::tag_t<test_cpo>, Foo&>;
  static_assert(std::is_void_v<foo_res>);
  static_assert(ae::TagInvocable_v<ae::tag_t<test_cpo>, Foo&>);
  static_assert(!ae::TagInvocable_v<ae::tag_t<test_get_int>, Foo&>);

  using bar_res = ae::tag_invoke_result_t<ae::tag_t<test_get_int>, Bar const&>;
  static_assert(std::is_same_v<int, bar_res>);
  static_assert(ae::TagInvocable_v<ae::tag_t<test_get_int>, Bar const&>);
  static_assert(!ae::TagInvocable_v<ae::tag_t<test_cpo>, Bar const&>);

  static_assert(ae::TagInvocable<Foo, ae::tag_t<test_cpo>>);
  static_assert(!ae::TagInvocable<Foo, ae::tag_t<test_get_int>>);
  static_assert(ae::TagInvocable<Bar, ae::tag_t<test_get_int>>);
  static_assert(!ae::TagInvocable<Bar, ae::tag_t<test_cpo>>);

  bool invoked{};
  Foo foo{invoked};
  test_cpo(foo);
  TEST_ASSERT_TRUE(invoked);

  Bar bar{42};
  auto value = test_get_int(bar);
  TEST_ASSERT_EQUAL(42, value);
}
}  // namespace test::tag_invoke_h
#endif

#endif  // AETHER_META_TAG_INVOKE_H_
