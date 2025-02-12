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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_WRITE_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_WRITE_H_

#include <vector>
#include <utility>

#include "aether/format/format.h"

#include "send_message_delays/delay_statistics.h"

namespace ae::bench {
class StatisticsWriteCsv {
  friend struct Formatter<StatisticsWriteCsv>;

 public:
  explicit StatisticsWriteCsv(
      std::vector<std::pair<std::string, DurationStatistics>> statistics);

 private:
  std::vector<std::pair<std::string, DurationStatistics>> statistics_;
};
}  // namespace ae::bench

namespace ae {
template <>
struct Formatter<bench::StatisticsWriteCsv> {
  template <typename TStream>
  void Format(const bench::StatisticsWriteCsv& value,
              FormatContext<TStream>& ctx) const {
    // print quick statistics
    ctx.out().write(
        std::string_view{"test name,max us,99% us,50% us,min us\n"});
    for (auto const& [name, statistics] : value.statistics_) {
      ae::Format(ctx.out(), "{},{},{},{},{}\n", name,
                 statistics.max_value().count(),
                 statistics.get_99th_percentile().count(),
                 statistics.get_50th_percentile().count(),
                 statistics.min_value().count());
    }

    // print legend
    ctx.out().write(std::string_view{"raw results\nmessage num,"});
    for (auto [name, _] : value.statistics_) {
      ae::Format(",{}", name);
    }
    // print data
    for (std::size_t i = 0;
         i < value.statistics_.begin()->second.raw_data().size(); ++i) {
      ctx.out().stream() << i;
      for (auto const& [_, statistics] : value.statistics_) {
        ctx.out().stream() << ',' << statistics.raw_data()[i].second.count();
      }
      ctx.out().stream() << '\n';
    }
    ctx.out().stream() << '\n';
  }
};
}  // namespace ae
#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_STATISTICS_WRITE_H_
