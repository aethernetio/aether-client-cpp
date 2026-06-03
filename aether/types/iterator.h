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

#ifndef AETHER_TYPES_ITERATOR_H_
#define AETHER_TYPES_ITERATOR_H_

#include <iterator>
#include <concepts>
#include <optional>

namespace ae {
namespace iterator_internal {
template <typename T>
  requires(std::is_integral_v<T>)
class Range {
 public:
  using value_type = T;

  Range(T start, T end, T step = 1) noexcept
      : start_{start}, end_{end}, step_{step}, reverse_{start_ > end_} {}

  std::optional<T> Next() noexcept {
    if (!reverse_ && (start_ <= end_)) {
      auto val = start_;
      start_ += step_;
      return val;
    }
    if (reverse_ && (start_ >= end_)) {
      auto val = start_;
      start_ -= step_;
      return val;
    }
    return std::nullopt;
  }

 private:
  T start_;
  T const end_;
  T const step_;
  bool reverse_;
};

template <typename T>
  requires(requires { typename std::iterator_traits<T>::value_type; })
class Iter {
 public:
  using value_type = typename std::iterator_traits<T>::value_type;
  using deref_type = decltype(*std::declval<T&>());

  template <typename U>
  struct Opt {
    explicit constexpr Opt(U* ref) noexcept : ref_{ref} {}

    explicit constexpr operator bool() const noexcept { return has_value(); }
    constexpr bool has_value() const noexcept { return ref_ != nullptr; }
    constexpr decltype(auto) value() const noexcept {
      assert(ref_ != nullptr && "Deref null value");
      return *ref_;
    }
    constexpr decltype(auto) operator*() const noexcept { return *ref_; }

   private:
    U* ref_{};
  };

  constexpr Iter(T&& begin, T&& end) noexcept
      : begin_{std::move(begin)}, end_{std::move(end)} {}

  constexpr auto Next() noexcept {
    using val = std::remove_pointer_t<decltype(&*begin_)>;
    if (begin_ != end_) {
      return Opt<val>{&*begin_++};
    }
    return Opt<val>{nullptr};
  }

 private:
  T begin_;
  T const end_;
};

};  // namespace iterator_internal

template <typename T>
concept IsIter = requires(T t) {
  typename T::value_type;
  // check if next return something bool like
  { !!t.Next() } -> std::same_as<bool>;
  { *t.Next() } -> std::convertible_to<typename T::value_type>;
};

using iterator_internal::Iter;
using iterator_internal::Range;
}  // namespace ae

#if AE_TESTS
#  include "tests/inline.h"

#  include <string>

namespace tests::iterator_h {
using namespace ae;  // NOLINT

AE_TEST_INLINE(test_IsIterConcept) {
  std::string hello = "Hello World!";
  auto str_iter = Iter{hello.begin(), hello.end()};
  static_assert(IsIter<decltype(str_iter)>);

  auto range_iter = Range{0, 10};
  static_assert(IsIter<decltype(range_iter)>);
  TEST_PASS();
}

AE_TEST_INLINE(test_StrIter) {
  std::string const hello = "Hello!";
  auto str_iter = Iter{hello.begin(), hello.end()};

  TEST_ASSERT_EQUAL('H', *str_iter.Next());
  TEST_ASSERT_EQUAL('e', *str_iter.Next());
  TEST_ASSERT_EQUAL('l', *str_iter.Next());
  TEST_ASSERT_EQUAL('l', *str_iter.Next());
  TEST_ASSERT_EQUAL('o', *str_iter.Next());
  TEST_ASSERT_EQUAL('!', *str_iter.Next());
  TEST_ASSERT_EQUAL(false, str_iter.Next().has_value());

  auto str_empty_iter = Iter{hello.end(), hello.end()};
  TEST_ASSERT_EQUAL(false, str_empty_iter.Next().has_value());
}

AE_TEST_INLINE(test_RangeIter) {
  auto range_iter = Range{0, 5, 2};

  TEST_ASSERT_EQUAL(0, *range_iter.Next());
  TEST_ASSERT_EQUAL(2, *range_iter.Next());
  TEST_ASSERT_EQUAL(4, *range_iter.Next());
  TEST_ASSERT_EQUAL(false, !!range_iter.Next());

  auto reverse_range = Range{5, 0};
  TEST_ASSERT_EQUAL(5, *reverse_range.Next());
  TEST_ASSERT_EQUAL(4, *reverse_range.Next());
  TEST_ASSERT_EQUAL(3, *reverse_range.Next());
  TEST_ASSERT_EQUAL(2, *reverse_range.Next());
  TEST_ASSERT_EQUAL(1, *reverse_range.Next());
  TEST_ASSERT_EQUAL(0, *reverse_range.Next());
  TEST_ASSERT_EQUAL(false, !!reverse_range.Next());
}
}  // namespace tests::iterator_h
#endif

#endif  // AETHER_TYPES_ITERATOR_H_
