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

#ifndef AETHER_PACKED_INT_H_
#define AETHER_PACKED_INT_H_

#include <tuple>
#include <array>
#include <limits>
#include <cstdint>
#include <type_traits>

#include "aether/mstream.h"

namespace ae {

namespace _internal {
using RangeTypeList =
    std::tuple<std::uint8_t, std::uint16_t, std::uint32_t, std::uint64_t>;

template <typename T, typename TypeList, std::size_t... Is>
constexpr auto RangeIndexImpl(std::index_sequence<Is...> const&) {
  constexpr auto arr =
      std::array{std::is_same_v<T, std::tuple_element_t<Is, TypeList>>...};

  std::size_t res = 0;
  for (; res < arr.size(); ++res) {
    if (arr[res]) {
      break;
    }
  }
  return res;
}

template <typename T, typename TypeList = RangeTypeList>
constexpr std::size_t RangeIndex() {
  return RangeIndexImpl<T, TypeList>(
      std::make_index_sequence<std::tuple_size_v<TypeList>>());
}
}  // namespace _internal

/**
 * \brief Limit variable calculations for ranges.
 */
template <typename MaxStored, typename MinStored, MinStored MinMaxStoredValue>
struct Limit {
  using StoredType = MaxStored;
  static constexpr auto kRangeIndex = _internal::RangeIndex<MaxStored>();

  using PrevRangeLimit =
      Limit<std::tuple_element_t<kRangeIndex - 1, _internal::RangeTypeList>,
            MinStored, MinMaxStoredValue>;

  static constexpr auto kAbsoluteMaxValue =
      std::numeric_limits<StoredType>::max();

  static constexpr auto kP = static_cast<MaxStored>(1)
                             << (std::numeric_limits<StoredType>::digits >> 1);

  static constexpr auto kMaxStored =
      (static_cast<StoredType>(PrevRangeLimit::kMaxStored)) -
      PrevRangeLimit::kBorrowCount +
      PrevRangeLimit::kBorrowCount *
          (static_cast<StoredType>(PrevRangeLimit::kAbsoluteMaxValue) + 1);
  static constexpr auto kUpper =
      kMaxStored - static_cast<StoredType>(PrevRangeLimit::kP);

  static constexpr auto kBorrowCount = PrevRangeLimit::kP;
};

/**
 * \brief Specialization for the first range
 */
template <typename Stored, Stored MaxStoredValue>
struct Limit<Stored, Stored, MaxStoredValue> {
  using StoredType = Stored;
  static constexpr auto kRangeIndex = _internal::RangeIndex<StoredType>();
  static constexpr auto kAbsoluteMaxValue =
      std::numeric_limits<StoredType>::max();
  static constexpr auto kBorrowCount =
      std::numeric_limits<StoredType>::max() - MaxStoredValue;
  static constexpr std::uint64_t kMaxStored = kAbsoluteMaxValue + 1;
  static constexpr auto kUpper =
      std::numeric_limits<StoredType>::max() - kBorrowCount + 1;
  static constexpr auto kP = static_cast<Stored>(1)
                             << (std::numeric_limits<Stored>::digits >> 1);
};

enum class PackedDeserializeRes {
  kNo,        // < not read
  kFinished,  // < read whole value
  kNext,      // < read only part of value
};

/**
 * \brief Variable length integer value.
 * All values divided on ranges and stored in a variable length format, e.g.
 * [0..250] as std::uint8_t, [251..1514] as std::uint16_t, [1515..1049834] as
 * std::uint32_t. The algorithm is optimized to store as more values as possible
 * in the first range and quite enough in the others. In contrast to other
 * algorithms it doesn't reserve special bits to indicate that there are more
 * ranges to read and this allows to more flexible settings how many values
 * stored in ranges.
 * Range calculation is not obvious and depends on max value
 * stored as min type (MinMaxStoredValue). For MinStoredType range always would
 * be a [0 - MinMaxStoredValue]. To calculate other ranges a few special
 * variables should be calculated:
 * - kP - is always 2^(half of digits in range's type) (? TODO: explain why ?)
 * - kBorrowCount - it is a count of values that next range could borrow from
 * current range. It is calculated as (type's max value) -  MinMaxStoredValue
 * for first range and as (kP from previous range) for others.
 * - kMaxStored - is a max count of values that can be stored in current range.
 * For first range it is always (type's max value + 1). For other ranges it is
 * calculated as (prev range kMaxStored) + (prev range kBorrowCount) * (prev
 * range's type max value + 1).
 * - kUpper - is a post max value in a current range. For the first range this
 * calculated as type's max value - kBorrowCount + 1. For other ranges it is
 * kMaxStored - (prev range kP).
 * So range would be [pre range's kUpper..kUpper).
 * All variables calculations are made in compile time by Limit type \see Limit.
 *
 * Serialize algorithm:
 *  - V is a currently stored value, and it can be divided in to two words: low
 * and high.
 *  - PrevUpper is a kUpper value for previous range.
 * Algorithm starts from the highest range and goes down to the first - the
 * minimum one.
 * void Serialize(V) { if (V >= PrevUpper) { V -= PrevUpper;
 *     high_part = V.high + PrevUpper;
 *     Save(high_part);
 *     write(V.low);
 *   }
 * }
 * and for first range:
 * void Serialize(V) {
 *   write(V);
 * }
 *
 * Deserialize algorithm:
 * V is a value to that we read
 * Algorithm starts from the first range and goes up to the highest one.
 * Res Deserialize(V) {
 *    res = Deserialize(V.high);
 *    if (res is finished) {
 *      V = V.high;
 *    }
 *    else if (res is next)
 *   {
 *     read(V.low);
 *     V = V + PrevUpper;
 *     if (V >= kUpper) {
 *        V = V - kUpper;
 *        return next;
 *     }
 *     return finished;
 *   }
 * }
 * and for first range:
 * Res Deserialize(V) {
 *   read(V);
 *   if (V >= kUpper) {
 *     V = V - kUpper;
 *     return next;
 *   }
 *   return finished;
 * }
 *
 * \tparam MaxStoredType - max value type that can be stored
 * \tparam MinStoredType - min value type that can be stored
 * \tparam MinMaxStoredValue - max value that can be saved in a
 * sizeof(MinStoredType)
 */
template <typename MaxStoredType, typename MinStoredType,
          MinStoredType MinMaxStoredValue>
struct Packed {
  using LimitType = Limit<MaxStoredType, MinStoredType, MinMaxStoredValue>;
  using ValueType = typename LimitType::StoredType;
  using PrevType = typename LimitType::PrevRangeLimit::StoredType;
  using PrevPacked = Packed<PrevType, MinStoredType, MinMaxStoredValue>;

  static constexpr ValueType kUpper = LimitType::kUpper;

#pragma pack(push, 1)
  union Storage {
    ValueType value;
    struct {
      PrevType low;
      PrevType high;
    } st;
  };
#pragma pack(pop)

  template <typename Tother>
  Packed(Tother v) {
    value.value = static_cast<ValueType>(v);
  }
  Packed() = default;

  operator ValueType&() noexcept { return value.value; }
  operator ValueType const&() const noexcept { return value.value; }

  template <typename TStream>
  PackedDeserializeRes Deserialize(TStream& is) {
    constexpr auto prev_upper = PrevPacked::kUpper;

    auto high = PrevPacked{};
    auto res = high.Deserialize(is);
    switch (res) {
      case PackedDeserializeRes::kNo:
        return res;
      case PackedDeserializeRes::kFinished: {
        value.value = static_cast<ValueType>(
            static_cast<typename PrevPacked::ValueType>(high));
        return PackedDeserializeRes::kFinished;
      }
      case PackedDeserializeRes::kNext: {
        value.st.high = static_cast<typename PrevPacked::ValueType>(high);
        is >> value.st.low;
        if (!data_was_read(is)) {
          return PackedDeserializeRes::kNo;
        }
        value.value += prev_upper;
        if (value.value >= kUpper) {
          value.value -= kUpper;
          return PackedDeserializeRes::kNext;
        }
        break;
      }
    }
    return PackedDeserializeRes::kFinished;
  }

  template <typename TStream>
  void Serialize(TStream& os) const {
    constexpr auto prev_upper = PrevPacked::kUpper;

    if (value.value >= prev_upper) {
      auto modified = value;
      modified.value -= prev_upper;
      PrevPacked{modified.st.high + prev_upper}.Serialize(os);
      os << modified.st.low;
      return;
    }
    PrevPacked{value.st.low}.Serialize(os);
  }

  Storage value;
};

/**
 * \brief Specialization for the first range.
 */
template <typename StoredType, StoredType MaxValue>
struct Packed<StoredType, StoredType, MaxValue> {
  using LimitType = Limit<StoredType, StoredType, MaxValue>;
  using ValueType = typename LimitType::StoredType;

  static constexpr ValueType kUpper = LimitType::kUpper;

  struct Storage {
    ValueType value;
  };

  template <typename Tother>
  Packed(Tother v) {
    value.value = static_cast<ValueType>(v);
  }
  Packed() = default;

  operator ValueType&() noexcept { return value.value; }
  operator ValueType const&() const noexcept { return value.value; }

  template <typename TStream>
  PackedDeserializeRes Deserialize(TStream& is) {
    is >> value.value;
    if (!data_was_read(is)) {
      return PackedDeserializeRes::kNo;
    }

    if (value.value >= kUpper) {
      value.value -= kUpper;
      return PackedDeserializeRes::kNext;
    } else {
      return PackedDeserializeRes::kFinished;
    }
  }

  template <typename TStream>
  void Serialize(TStream& os) const {
    os << value.value;
  }

  Storage value;
};

template <typename TStream, typename T, typename Min, Min MinMaxVal>
TStream& operator<<(TStream& os, Packed<T, Min, MinMaxVal> const& v) {
  v.Serialize(os);
  return os;
}

template <typename TStream, typename T, typename Min, Min MinMaxVal>
TStream& operator>>(TStream& is, Packed<T, Min, MinMaxVal>& v) {
  v.Deserialize(is);
  return is;
}

template <typename T1, typename Min1, Min1 MinMaxVal1, typename T2,
          typename Min2, Min2 MinMaxVal2>
int PackedCompare(Packed<T1, Min1, MinMaxVal1> const& left,
                  Packed<T2, Min2, MinMaxVal2> const& right) {
  if (left.value.value < right.value.value) {
    return -1;
  } else if (left.value.value > right.value.value) {
    return 1;
  } else {
    return 0;
  }
}

template <typename T1, typename Min1, Min1 MinMaxVal1, typename T2,
          typename Min2, Min2 MinMaxVal2>
static bool operator==(Packed<T1, Min1, MinMaxVal1> const& left,
                       Packed<T2, Min2, MinMaxVal2> const& right) {
  return PackedCompare(left, right) == 0;
}

template <typename T1, typename Min1, Min1 MinMaxVal1, typename T2,
          typename Min2, Min2 MinMaxVal2>
static bool operator<(Packed<T1, Min1, MinMaxVal1> const& left,
                      Packed<T2, Min2, MinMaxVal2> const& right) {
  return PackedCompare(left, right) < 0;
}

template <typename T1, typename Min1, Min1 MinMaxVal1, typename T2,
          typename Min2, Min2 MinMaxVal2>
static bool operator>(Packed<T1, Min1, MinMaxVal1> const& left,
                      Packed<T2, Min2, MinMaxVal2> const& right) {
  return PackedCompare(left, right) > 0;
}

}  // namespace ae

namespace std {
template <typename T, typename Min, Min MinMaxVal>
class numeric_limits<ae::Packed<T, Min, MinMaxVal>> {
 public:
  static constexpr T lowest() { return T{0}; }
  static constexpr T min() { return T{0}; }
  static constexpr T max() { return ae::Packed<T, Min, MinMaxVal>::kUpper; }
};

template <typename T, typename Min, Min MinMaxVal>
struct hash<ae::Packed<T, Min, MinMaxVal>> {
  std::size_t operator()(ae::Packed<T, Min, MinMaxVal> const& packed) const {
    return static_cast<std::size_t>(static_cast<T>(packed));
  }
};
}  // namespace std

#endif  // AETHER_PACKED_INT_H_ */
