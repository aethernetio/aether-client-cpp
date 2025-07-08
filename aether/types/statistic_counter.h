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

#ifndef AETHER_TYPES_STATISTIC_COUNTER_H_
#define AETHER_TYPES_STATISTIC_COUNTER_H_

#include <cmath>
#include <cstddef>
#include <cassert>
#include <algorithm>
#include <type_traits>

#include "aether/warning_disable.h"

DISABLE_WARNING_PUSH()
IGNORE_IMPLICIT_CONVERSION()
#include "third_party/etl/include/etl/circular_buffer.h"
DISABLE_WARNING_POP()

#include "aether/common.h"
#include "aether/mstream.h"
#include "aether/format/format.h"

namespace ae {
template <typename TValue, std::size_t Capacity,
          typename Comparator = std::less<TValue>>
class StatisticsCounter final {
  friend struct Formatter<StatisticsCounter<TValue, Capacity, Comparator>>;

 public:
  StatisticsCounter() noexcept = default;

  AE_CLASS_COPY_MOVE(StatisticsCounter)

  template <typename TIterator,
            AE_REQUIRERS((std::is_same<
                          TValue,
                          std::decay_t<decltype(*std::declval<TIterator>())>>))>
  void Insert(TIterator const& begin, TIterator const& end) {
    value_buffer_.push(begin, end);
  }

  template <typename U, AE_REQUIRERS((std::is_same<TValue, std::decay_t<U>>))>
  void Add(U&& value) {
    value_buffer_.push(std::forward<U>(value));
  }

  [[nodiscard]] TValue max() const {
    assert(!value_buffer_.empty());
    return *std::max_element(std::begin(value_buffer_), std::end(value_buffer_),
                             Comparator{});
  }

  [[nodiscard]] TValue min() const {
    assert(!value_buffer_.empty());
    return *std::min_element(std::begin(value_buffer_), std::end(value_buffer_),
                             Comparator{});
  }

  /**
   * \brief Get a particular percentile value.
   * Percentile must be in range [0, 100].
   */
  template <std::size_t Percentile>
  [[nodiscard]] TValue percentile() const {
    static_assert((Percentile >= 0) && (Percentile <= 100),
                  "Percentile must be in [0,100]% range");

    if constexpr (Percentile == 0) {
      return min();
    } else if constexpr (Percentile == 100) {
      return max();
    } else {
      assert(!value_buffer_.empty());
      auto sorted_list = value_buffer_;
      std::sort(std::begin(sorted_list), std::end(sorted_list), Comparator{});

      auto index = static_cast<typename decltype(sorted_list)::size_type>(  //
          std::ceil(static_cast<double>(sorted_list.size() - 1) * Percentile /
                    100.0));
      return sorted_list[index];
    }
  }

  std::size_t size() const { return value_buffer_.size(); }
  bool empty() const { return value_buffer_.empty(); }

  template <typename Ib>
  friend imstream<Ib>& operator>>(imstream<Ib>& is, StatisticsCounter& value) {
    typename Ib::size_type size;
    is >> size;
    for (std::size_t i = 0; (i < static_cast<std::size_t>(size)) &&
                            (i < value.value_buffer_.max_size());
         ++i) {
      TValue temp;
      is >> temp;
      value.value_buffer_.push(std::move(temp));
    }
    return is;
  }

  template <typename Ob>
  friend omstream<Ob>& operator<<(omstream<Ob>& os,
                                  StatisticsCounter const& value) {
    os << static_cast<typename Ob::size_type>(value.value_buffer_.size());
    for (auto const& v : value.value_buffer_) {
      os << v;
    }
    return os;
  }

 private:
  etl::circular_buffer<TValue, Capacity> value_buffer_;
};

/**
 * \brief Formatter implementation.
 */
template <typename T, std::size_t Capacity, typename Comparator>
struct Formatter<StatisticsCounter<T, Capacity, Comparator>>
    : public Formatter<etl::circular_buffer<T, Capacity>> {
  template <typename TStream>
  void Format(StatisticsCounter<T, Capacity, Comparator> const& value,
              FormatContext<TStream>& ctx) const {
    Formatter<etl::circular_buffer<T, Capacity>>{}.Format(value.value_buffer_,
                                                          ctx);
  }
};

}  // namespace ae
#endif  // AETHER_TYPES_STATISTIC_COUNTER_H_
