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

#ifndef AETHER_CHANNELS_CHANNEL_STATISTICS_H_
#define AETHER_CHANNELS_CHANNEL_STATISTICS_H_

#include "aether/config.h"
#include "aether/common.h"
#include "aether/obj/obj.h"

#include "aether/types/statistic_counter.h"

namespace ae {
class ChannelStatistics final : public Obj {
  AE_OBJECT(ChannelStatistics, Obj, 0)
  ChannelStatistics() = default;

  static constexpr std::size_t kConnectionWindowSize =
      AE_STATISTICS_CONNECTION_WINDOW_SIZE;
  static constexpr std::size_t kResponseWindowSize =
      AE_STATISTICS_RESPONSE_WINDOW_SIZE;

  using ConnectionTimeStatistics =
      StatisticsCounter<Duration, kConnectionWindowSize>;
  using ResponseTimeStatistics =
      StatisticsCounter<Duration, kResponseWindowSize>;

 public:
  explicit ChannelStatistics(ObjProp prop);

  AE_OBJECT_REFLECT(AE_MMBRS(connection_time_statistics_,
                             response_time_statistics_))

  void AddConnectionTime(Duration duration);
  void AddResponseTime(Duration duration);

  ConnectionTimeStatistics const& connection_time_statistics() const {
    return connection_time_statistics_;
  }

  ResponseTimeStatistics const& response_time_statistics() const {
    return response_time_statistics_;
  }

 private:
  ConnectionTimeStatistics connection_time_statistics_;
  ResponseTimeStatistics response_time_statistics_;
};

}  // namespace ae

#endif  // AETHER_CHANNELS_CHANNEL_STATISTICS_H_
