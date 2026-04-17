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

#ifndef AETHER_TYPES_RING_INDEX_H_
#define AETHER_TYPES_RING_INDEX_H_

#include <limits>
#include <cstdlib>

#include "aether/format/format.h"

namespace ae {
template <typename Index>
struct IndexComparable;

/**
 * \brief Struct to apply addition and subtraction operations to ring buffer
 * index.
 * Value is always in range [0, Max] and value overflow leads to
 * start from zero
 */
template <std::size_t Max = (std::numeric_limits<std::size_t>::max() / 2) - 1>
  requires(Max < std::numeric_limits<std::size_t>::max() / 2)
struct RingIndex {
  friend struct IndexComparable<RingIndex>;

  using type = std::size_t;
  static constexpr type max = Max;

  constexpr RingIndex() noexcept : value_{} {}
  explicit constexpr RingIndex(std::size_t val) noexcept : value_{val % max} {}

  constexpr std::size_t Distance(RingIndex other) const noexcept {
    auto a = max - value_;
    auto b = max - other.value_;
    if (a >= b) {
      return a - b;
    }
    return a + other.value_;
  }

  friend constexpr std::size_t Distance(RingIndex one,
                                        RingIndex another) noexcept {
    return one.Distance(another);
  }

  constexpr bool operator==(RingIndex other) const noexcept {
    return value_ == other.value_;
  }
  constexpr bool operator!=(RingIndex other) const noexcept {
    return value_ != other.value_;
  }

  constexpr RingIndex& operator+=(std::size_t val) noexcept {
    value_ = (value_ + val) % max;
    return *this;
  }

  constexpr RingIndex& operator-=(std::size_t val) noexcept {
    value_ = (val > value_) ? (max - val + value_) : (value_ - val);
    value_ = value_ % max;
    return *this;
  }

  friend constexpr RingIndex operator+(RingIndex index,
                                       std::size_t val) noexcept {
    index += val;
    return index;
  }

  friend constexpr RingIndex operator-(RingIndex index,
                                       std::size_t val) noexcept {
    index -= val;
    return index;
  }

  constexpr RingIndex& operator++() noexcept {
    value_ = (value_ + 1) % max;
    return *this;
  }
  constexpr RingIndex operator++(int) noexcept {
    RingIndex tmp = *this;
    value_ = (value_ + 1) % max;
    return tmp;
  }
  constexpr RingIndex& operator--() noexcept {
    value_ = (value_ - 1) % max;
    return *this;
  }
  constexpr RingIndex operator--(int) noexcept {
    RingIndex tmp = *this;
    value_ = (value_ - 1) % max;
    return tmp;
  }

  explicit constexpr operator std::size_t() const { return value_; }

  template <typename T>
    requires(std::is_integral_v<T>)
  explicit constexpr operator T() const {
    return static_cast<T>(value_);
  }

 private:
  std::size_t value_;
};

template <std::size_t M>
struct Formatter<RingIndex<M>> : Formatter<std::size_t> {
  template <typename TStream>
  void Format(RingIndex<M> const& index, FormatContext<TStream>& ctx) const {
    Formatter<std::size_t>::Format(static_cast<std::size_t>(index), ctx);
  }
};

template <typename Index>
struct IndexComparable {
  constexpr bool operator>(Index other) const noexcept {
    return this->IsAfter(other);
  }
  constexpr bool operator>=(Index other) const noexcept {
    return (value == other) || this->IsAfter(other);
  }
  constexpr bool operator<(Index other) const noexcept {
    return this->IsBefore(other);
  }
  constexpr bool operator<=(Index other) const noexcept {
    return (value == other) || this->IsBefore(other);
  }

  constexpr bool IsBefore(Index other) const noexcept {
    return (value != other) && (begin.Distance(value) < begin.Distance(other));
  }

  constexpr bool IsAfter(Index other) const noexcept {
    return (value != other) && (begin.Distance(value) > begin.Distance(other));
  }

  Index value;
  Index begin;
};

template <typename Index>
struct RingIndexRange {
  // index is in [left:right] range
  constexpr bool InRange(Index index, Index begin) const noexcept {
    return (IndexComparable{left, begin} <= index) &&
           (IndexComparable{right, begin} >= index);
  }
  // index range is before index ( right < index)
  constexpr bool IsBefore(Index index, Index begin) const {
    return IndexComparable{right, begin} < index;
  }
  // index range is after index ( left > index)
  constexpr bool IsAfter(Index index, Index begin) const {
    return IndexComparable{left, begin} > index;
  }
  // index range is flipped ( left > right)
  constexpr bool IsFlipped(Index begin) const {
    return IndexComparable{left, begin} > right;
  }
  // index range is empty ( left == right)
  constexpr bool IsEmpty() const { return left == right; }

  constexpr auto distance() const { return Distance(left, right); }

  constexpr bool operator==(RingIndexRange const& other) const {
    return (left == other.left) && (right == other.right);
  }
  constexpr bool operator!=(RingIndexRange const& other) const {
    return !(*this == other);
  }

  Index left;
  Index right;
};

}  // namespace ae

#endif  // AETHER_TYPES_RING_INDEX_H_
