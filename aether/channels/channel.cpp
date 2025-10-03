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

#include "aether/channels/channel.h"

#include <chrono>
#include <utility>

#include "aether/config.h"

namespace ae {

Channel::Channel(UnifiedAddress address, Domain* domain)
    : Obj{domain},
      address{std::move(address)},
      channel_statistics_{domain->CreateObj<ChannelStatistics>()} {
  channel_statistics_->AddResponseTime(
      std::chrono::milliseconds{AE_DEFAULT_RESPONSE_TIMEOUT_MS});
  channel_statistics_->AddConnectionTime(
      std::chrono::milliseconds{AE_DEFAULT_CONNECTION_TIMEOUT_MS});
}

ChannelTransportProperties const& Channel::transport_properties() const {
  return transport_properties_;
}

ChannelStatistics& Channel::channel_statistics() {
  return *channel_statistics_;
}

Duration Channel::TransportBuildTimeout() const {
  return channel_statistics_->connection_time_statistics().percentile<99>();
}

Duration Channel::ResponseTimeout() const {
  return channel_statistics_->response_time_statistics().percentile<99>();
}

}  // namespace ae
