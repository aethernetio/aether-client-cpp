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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_H_

#include "send_message_delays/time_table.h"

namespace ae::bench {

class DurationStatistics {
 public:
  explicit DurationStatistics(DurationTable data);

  Duration max_value() const;
  Duration min_value() const;
  Duration get_99th_percentile() const;
  Duration get_50th_percentile() const;
  DurationTable const& raw_data() const;
  DurationTable const& sorted_data() const;

 private:
  static DurationTable MakeSorted(DurationTable const& data);
  static std::size_t GetPercentileIndex(std::size_t size,
                                        std::size_t percentile);
  static Duration GetValueByIndex(DurationTable const& data, std::size_t index);

  DurationTable raw_data_;
  DurationTable sorted_data_;
  Duration v99th_percentile_;
  Duration v50th_percentile_;
  Duration max_value_;
  Duration min_value_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_H_
