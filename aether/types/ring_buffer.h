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

#ifndef AETHER_TYPES_RING_BUFFER_H_
#define AETHER_TYPES_RING_BUFFER_H_

#include <cstdlib>
#include <limits>
#include <type_traits>

#include "aether/format/format.h"

namespace ae {

/**
 * \brief Struct to apply addition and subtraction operations to ring buffer
 * index.
 * Value is always in range [0, Max] and value overflow leads to
 * start from zero
 * The window size is set as Max/2 - 1.
 */
template <typename T, T Max = std::numeric_limits<T>::max(),
          AE_REQUIRERS((std::is_integral<T>))>
struct RingIndex {
  using type = T;
  static constexpr T max = Max;
  static constexpr T window_size = (Max / 2) - 1;

  constexpr RingIndex() : value_{Mod(0)} {}
  explicit constexpr RingIndex(T val) : value_{Mod(val)} {}

  constexpr void Clockwise(T val) {
    val = Mod(val);
    if ((Max - val + 1) < value_) {
      // -1 for zero value
      value_ = static_cast<T>(val - (Max - value_));
    } else {
      value_ += val;
    }
  }
  constexpr void CounterClockwise(T val) {
    val = Mod(val);
    if (value_ < val) {
      value_ = static_cast<T>(Max - (val - value_) + 1);
    } else {
      value_ -= val;
    }
  }

  constexpr T Distance(RingIndex other) const {
    auto a = Max - value_;
    auto b = Max - other.value_;
    if (a >= b) {
      return static_cast<T>(a - b);
    }
    return static_cast<T>(a + other.value_);
  }

  constexpr bool IsBefore(RingIndex other) const {
    return (value_ != other.value_) && (Distance(other) < window_size);
  }

  constexpr bool IsAfter(RingIndex other) const {
    return (value_ != other.value_) && (Distance(other) > window_size);
  }

  constexpr bool operator==(RingIndex other) const {
    return value_ == other.value_;
  }

  constexpr bool operator!=(RingIndex other) const {
    return value_ != other.value_;
  }

  constexpr RingIndex& operator+=(T val) {
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        CounterClockwise(std::abs(val));
        return *this;
      }
    }
    if (val > 0) {
      Clockwise(val);
    }
    return *this;
  }

  constexpr RingIndex& operator-=(T val) {
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        Clockwise(std::abs(val));
        return *this;
      }
    }
    if (val > 0) {
      CounterClockwise(val);
    }
    return *this;
  }

  friend constexpr RingIndex operator+(RingIndex index, T val) {
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        index.CounterClockwise(std::abs(val));
        return index;
      }
    }
    if (val > 0) {
      index.Clockwise(val);
    }
    return index;
  }

  friend constexpr RingIndex operator-(RingIndex index, T val) {
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        index.Clockwise(std::abs(val));
        return index;
      }
    }
    if (val > 0) {
      index.CounterClockwise(val);
    }
    return index;
  }

  constexpr RingIndex& operator++() {
    Clockwise(1);
    return *this;
  }
  constexpr RingIndex operator++(int) {
    RingIndex tmp = *this;
    Clockwise(1);
    return tmp;
  }
  constexpr RingIndex& operator--() {
    CounterClockwise(1);
    return *this;
  }
  constexpr RingIndex operator--(int) {
    RingIndex tmp = *this;
    CounterClockwise(1);
    return tmp;
  }

  explicit constexpr operator T() const { return value_; }

 private:
  static constexpr T Mod(T val) {
    if constexpr (Max != std::numeric_limits<T>::max()) {
      return val % (Max + 1);
    } else {
      return val;
    }
  }

  T value_;
};

template <typename T, T M>
struct Formatter<RingIndex<T, M>> : Formatter<T> {
  template <typename TStream>
  void Format(RingIndex<T, M> const& index, FormatContext<TStream>& ctx) const {
    Formatter<T>::Format(static_cast<T>(index), ctx);
  }
};

}  // namespace ae

#endif  // AETHER_TYPES_RING_BUFFER_H_
