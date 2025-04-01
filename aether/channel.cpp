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

#include "aether/channel.h"

namespace ae {

Channel::Channel(Domain* domain)
    : Obj{domain}, channel_statistics{domain->CreateObj<ChannelStatistics>()} {}

void Channel::AddConnectionTime(Duration connection_time) {
  channel_statistics->AddConnectionTime(std::move(connection_time));
}

void Channel::AddPingTime(Duration ping_time) {
  channel_statistics->AddPingTime(std::move(ping_time));
}

Duration Channel::expected_connection_time() const {
  if (channel_statistics->connection_time_statistics().empty()) {
    return default_connection_time;
  }
  return channel_statistics->connection_time_statistics().percentile<99>();
}

Duration Channel::expected_ping_time() const {
  if (channel_statistics->ping_time_statistics().empty()) {
    return default_ping_time;
  }
  return channel_statistics->ping_time_statistics().percentile<99>();
}

}  // namespace ae
