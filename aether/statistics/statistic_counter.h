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

#ifndef AETHER_STATISTICS_STATISTIC_COUNTER_H_
#define AETHER_STATISTICS_STATISTIC_COUNTER_H_

#include <cmath>
#include <cstddef>
#include <cassert>
#include <algorithm>

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
            std::enable_if_t<std::is_same_v<
                TValue, std::decay_t<decltype(*std::declval<TIterator>())>>>>
  void Insert(TIterator const& begin, TIterator const& end) {
    value_buffer_.push(begin, end);
    // keep buffer sorted for easier percentile calculation
    std::sort(std::begin(value_buffer_), std::end(value_buffer_), Comparator{});
  }

  void Add(TValue&& value) {
    value_buffer_.push(std::move(value));
    // TODO: maybe implement insert method for etl::icircular_buffer and use it
    // with std::lower_bound

    // keep buffer sorted for easier percentile calculation
    std::sort(std::begin(value_buffer_), std::end(value_buffer_), Comparator{});
  }

  TValue const& max() const {
    assert(!value_buffer_.empty());
    return value_buffer_.back();
  }

  TValue const& min() const {
    assert(!value_buffer_.empty());
    return value_buffer_.front();
  }

  /**
   * \brief Get a particular percentile value.
   * Percentile must be in range [0, 100].
   */
  template <std::size_t Percentile>
  TValue const& percentile() const {
    static_assert((Percentile >= 0) && (Percentile <= 100),
                  "Percentile must be in [0,100]% range");

    if constexpr (Percentile == 0) {
      return min();
    } else if constexpr (Percentile == 100) {
      return max;
    } else {
      assert(!value_buffer_.empty());
      auto index = static_cast<typename decltype(value_buffer_)::size_type>(  //
          std::round(                                                         //
              Percentile * static_cast<double>(value_buffer_.size() - 1) /
              100.0));
      return value_buffer_[index];
    }
  }

  std::size_t size() const { return value_buffer_.size(); }
  bool empty() const { return value_buffer_.empty(); }

  template <typename Ob>
  friend omstream<Ob>& operator<<(
      omstream<Ob>& os,
      StatisticsCounter<TValue, Capacity, Comparator> const& value) {
    os << static_cast<typename Ob::size_type>(value.value_buffer_.size());
    for (auto const& v : value.value_buffer_) {
      os << v;
    }
    return os;
  }

  template <typename Ib>
  friend imstream<Ib>& operator>>(
      imstream<Ib>& is,
      StatisticsCounter<TValue, Capacity, Comparator>& value) {
    typename Ib::size_type size;
    is >> size;
    for (std::size_t i = 0; (i < size) && (i < value.value_buffer_.max_size());
         ++i) {
      TValue temp;
      is >> temp;
      value.value_buffer_.push(std::move(temp));
    }
    return is;
  }

 private:
  etl::circular_buffer<TValue, Capacity> value_buffer_;
};

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
#endif  // AETHER_STATISTICS_STATISTIC_COUNTER_H_
