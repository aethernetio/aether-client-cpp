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

#include "send_message_delays/delay_statistics.h"

#include <cmath>
#include <utility>
#include <algorithm>

#include "aether/warning_disable.h"

namespace ae::bench {
DurationStatistics::DurationStatistics(DurationTable data)
    : raw_data_{std::move(data)},
      sorted_data_{MakeSorted(raw_data_)},
      v99th_percentile_{GetValueByIndex(
          sorted_data_, GetPercentileIndex(sorted_data_.size(), 99))},
      v50th_percentile_{GetValueByIndex(
          sorted_data_, GetPercentileIndex(sorted_data_.size(), 50))},
      max_value_{GetValueByIndex(sorted_data_, sorted_data_.size() - 1)},
      min_value_{GetValueByIndex(sorted_data_, 0)} {}

Duration DurationStatistics::max_value() const { return max_value_; }
Duration DurationStatistics::min_value() const { return min_value_; }

Duration DurationStatistics::get_99th_percentile() const {
  return v99th_percentile_;
}

Duration DurationStatistics::get_50th_percentile() const {
  return v50th_percentile_;
}

DurationTable DurationStatistics::MakeSorted(DurationTable const& data) {
  DurationTable sorted_data = data;
  // remove bad values
  sorted_data.erase(
      std::remove_if(std::begin(sorted_data), std::end(sorted_data),
                     [](auto const& item) { return !item.second; }),
      std::end(sorted_data));
  std::sort(sorted_data.begin(), sorted_data.end(),
            [](auto const& left, auto const& right) {
              return *left.second < *right.second;
            });
  return sorted_data;
}

DurationTable const& DurationStatistics::raw_data() const { return raw_data_; }
DurationTable const& DurationStatistics::sorted_data() const {
  return sorted_data_;
}

std::size_t DurationStatistics::GetPercentileIndex(std::size_t size,
                                                   std::size_t percentile) {
  return static_cast<std::size_t>(std::floor((static_cast<double>(size - 1)) *
                                             static_cast<double>(percentile) /
                                             100.0));
}

Duration DurationStatistics::GetValueByIndex(DurationTable const& data,
                                             std::size_t index) {
  if (index >= data.size()) {
    return {};
  }
  return *data[index].second;
}

}  // namespace ae::bench
