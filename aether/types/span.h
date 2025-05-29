/*
 * Copyright 2025 Aethernet Inc.
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

#ifndef AETHER_TYPES_SPAN_H_
#define AETHER_TYPES_SPAN_H_

#include <array>
#include <cstddef>

namespace ae {
template <typename T>
class Span {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using pointer_type = T*;
  using const_pointer_type = T const*;
  using iterator = pointer_type;
  using const_iterator = const_pointer_type;

  constexpr Span() : pointer_{nullptr}, size_{0} {}

  constexpr explicit Span(pointer_type pointer, size_type size)
      : pointer_{pointer}, size_{size} {}

  template <size_type Size>
  constexpr explicit Span(value_type (&arr)[Size])
      : pointer_{arr}, size_{Size} {}

  template <size_type Size>
  constexpr explicit Span(
      std::array<std::remove_const_t<value_type>, Size> const& arr)
      : pointer_{arr.data()}, size_{Size} {}

  template <size_type Size>
  constexpr explicit Span(std::array<value_type, Size>& arr)
      : pointer_{arr.data()}, size_{Size} {}

  constexpr iterator begin() const { return pointer_; }
  constexpr iterator cbegin() const { return pointer_; }

  constexpr iterator end() const { return pointer_ + size_; }
  constexpr iterator cend() const { return pointer_ + size_; }

  constexpr size_type size() const { return size_; }
  constexpr pointer_type data() const { return pointer_; }
  constexpr pointer_type data() { return pointer_; }
  constexpr Span sub(size_type pos, size_type count) const {
    return Span{pointer_ + pos, count};
  }

 private:
  pointer_type pointer_;
  size_type size_;
};

// deduction guides
template <typename T>
Span(T const*, std::size_t) -> Span<T const>;

template <typename T>
Span(T*, std::size_t) -> Span<T>;

template <typename T, std::size_t Size>
Span(T const (&arr)[Size]) -> Span<T const>;

template <typename T, std::size_t Size>
Span(T (&arr)[Size]) -> Span<T>;

template <typename T, std::size_t Size>
Span(std::array<T, Size> const& arr) -> Span<T const>;

template <typename T, std::size_t Size>
Span(std::array<T, Size>& arr) -> Span<T>;
}  // namespace ae

#endif  // AETHER_TYPES_SPAN_H_
